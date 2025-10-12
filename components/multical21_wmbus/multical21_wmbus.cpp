#include "multical21_wmbus.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <mbedtls/aes.h>

namespace esphome {
namespace multical21_wmbus {

// Static member initialization
Multical21WMBusComponent *Multical21WMBusComponent::isr_instance_ = nullptr;

void Multical21WMBusComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Multical21 wMBUS receiver...");

  // Initialize SPI first
  this->spi_setup();
  ESP_LOGD(TAG, "SPI initialized");

  // Small delay to let SPI settle
  delay(10);

  // Initialize and configure CC1101 radio via helper class
  radio_.init(this);  // Pass SPI interface
  radio_.reset();
  radio_.configure();
  radio_.start_rx();

  // Setup GDO0 interrupt (packet ready signal)
  // Store instance pointer for ISR access
  isr_instance_ = this;

  // Configure GDO0 pin as input (NO pullup - CC1101 GDO0 is a push-pull output)
  // Per working reference implementation and CC1101 datasheet:
  // GDO pins are active outputs and don't need pull-up/pull-down resistors
  pinMode(this->gdo0_pin_, INPUT);

  // Attach interrupt handler - FALLING edge triggers when packet reception completes
  // Per WMBUS_IMPLEMENTATION_SPEC.md Section 3.6:
  // - GDO0 goes HIGH when sync word is detected
  // - GDO0 goes LOW at end of packet (this triggers our interrupt)
  attachInterrupt(digitalPinToInterrupt(this->gdo0_pin_),
                  []() {
                    if (isr_instance_ != nullptr) {
                      Multical21WMBusComponent::packet_isr_(isr_instance_);
                    }
                  },
                  FALLING);
  ESP_LOGD(TAG, "GDO0 interrupt attached to GPIO%u (FALLING edge)", this->gdo0_pin_);

  this->last_packet_time_ = millis();
  this->last_health_check_ = millis();

  // Setup periodic health check using ESPHome's set_interval()
  // This is more efficient than checking in loop() every 7ms
  this->set_interval("health_check", HEALTH_CHECK_INTERVAL_MS, [this]() {
    this->log_radio_status_();
  });

  // Setup timeout check
  this->set_interval("timeout_check", 30000, [this]() {  // Check every 30 seconds
    uint32_t now = millis();
    if (now - this->last_packet_time_ > RECEIVE_TIMEOUT_MS) {
      ESP_LOGW(TAG, "No packets received for 5 minutes, restarting radio");
      radio_.reset();
      radio_.configure();
      radio_.start_rx();
      this->last_packet_time_ = now;
    }
  });

  ESP_LOGCONFIG(TAG, "Multical21 wMBUS receiver setup complete");
}

// ============================================================================
// Helper Functions
// ============================================================================

void Multical21WMBusComponent::update_meter_stats_(uint32_t meter_id_uint, const std::string &frame_type) {
  uint32_t now = millis();

  for (auto &stats : this->meter_stats_) {
    if (stats.meter_id == meter_id_uint) {
      if (stats.packet_count > 0) {
        uint32_t interval_ms = now - stats.last_seen_ms;
        stats.total_interval_ms += interval_ms;
        uint32_t avg_interval_sec = (stats.total_interval_ms / stats.packet_count) / 1000;
        ESP_LOGI(TAG, "Interval: %u.%u sec (avg: %u sec, count: %u, frame: %s)",
                 interval_ms / 1000, (interval_ms % 1000) / 100,
                 avg_interval_sec, stats.packet_count + 1, frame_type.c_str());
      }
      stats.last_seen_ms = now;
      stats.packet_count++;

      // Track frame types
      if (frame_type == "long") {
        stats.long_frame_count++;
      } else if (frame_type == "compact") {
        stats.compact_frame_count++;
      }
      stats.last_frame_type = frame_type;
      return;
    }
  }

  // New meter detected
  MeterStats new_stats;
  new_stats.meter_id = meter_id_uint;
  new_stats.last_seen_ms = now;
  new_stats.packet_count = 1;
  new_stats.total_interval_ms = 0;
  new_stats.compact_frame_count = (frame_type == "compact") ? 1 : 0;
  new_stats.long_frame_count = (frame_type == "long") ? 1 : 0;
  new_stats.last_frame_type = frame_type;
  this->meter_stats_.push_back(new_stats);
  ESP_LOGI(TAG, "First packet from this meter (frame type: %s)", frame_type.c_str());
}

bool Multical21WMBusComponent::is_our_meter_id_(const uint8_t *meter_id_le) {
  // meter_id_le is little-endian from packet
  // this->meter_id_ is big-endian from config
  return (meter_id_le[0] == this->meter_id_[3] &&
          meter_id_le[1] == this->meter_id_[2] &&
          meter_id_le[2] == this->meter_id_[1] &&
          meter_id_le[3] == this->meter_id_[0]);
}

bool Multical21WMBusComponent::read_packet_from_fifo_(uint8_t *buffer, uint8_t &length) {
  // CRITICAL: Read ALL bytes from FIFO even if L-field is invalid
  // This prevents FIFO corruption by ensuring garbage packets are fully cleared

  // Read preamble (2 bytes, discard)
  this->radio_.read_fifo_byte();
  this->radio_.read_fifo_byte();

  // Read L-field
  length = this->radio_.read_fifo_byte();

  // Log every packet attempt for debugging
  ESP_LOGI(TAG, "Packet received: L-field=%u", length);

  // Basic sanity check to prevent crazy reads (but still read bytes after)
  if (length < 255 && length > 0) {
    // Store L-field in buffer
    buffer[0] = length;

    // Read ALL payload bytes from FIFO (capped at MAX_PACKET_SIZE to prevent buffer overflow)
    uint8_t bytes_to_read = (length < MAX_PACKET_SIZE) ? length : MAX_PACKET_SIZE;
    for (uint8_t i = 0; i < bytes_to_read; i++) {
      buffer[i + 1] = this->radio_.read_fifo_byte();
    }

    // If L-field was larger than MAX_PACKET_SIZE, drain excess bytes
    if (length > MAX_PACKET_SIZE) {
      uint8_t excess = length - MAX_PACKET_SIZE;
      ESP_LOGW(TAG, "Draining %u excess bytes (L-field=%u exceeds MAX=%u)", excess, length, MAX_PACKET_SIZE);
      for (uint8_t i = 0; i < excess; i++) {
        this->radio_.read_fifo_byte();
      }
    }

    // NOW validate the L-field for wMBUS protocol (AFTER reading all bytes)
    if (length < MIN_WMBUS_PACKET_LENGTH || length > MAX_PACKET_SIZE) {
      return false;  // Invalid packet length for wMBUS (but FIFO is already drained)
    }

    return true;  // Valid packet
  }

  // Crazy L-field (0 or 255) - drain what we can from FIFO
  ESP_LOGW(TAG, "Crazy L-field value: %u, attempting to drain FIFO", length);
  // Check actual FIFO contents and drain
  uint8_t rxbytes = this->radio_.get_rx_bytes();
  uint8_t remaining = (rxbytes & 0x7F);  // Mask off overflow bit
  // We already read 3 bytes (2 preamble + 1 L-field)
  if (remaining > 0) {
    ESP_LOGW(TAG, "Draining %u remaining bytes from FIFO after bad L-field", remaining);
    for (uint8_t i = 0; i < remaining && i < 64; i++) {  // Safety limit
      this->radio_.read_fifo_byte();
    }
  }
  return false;
}

bool Multical21WMBusComponent::read_fifo_into_packet_buffer_() {
  // Check if packet buffer has space
  if (this->packet_buffer_.is_full()) {
    ESP_LOGW(TAG, "Packet buffer full - dropping packet");
    return false;
  }

  // CRITICAL: Enter IDLE state BEFORE reading FIFO to prevent overflow condition!
  // Reading FIFO while in RX state can cause MARCSTATE 0x0D (RX_FIFO_OVERFLOW)
  // The FIFO contents are preserved when entering IDLE state.
  this->radio_.enter_idle();

  // Small delay to ensure state transition completes
  delayMicroseconds(100);

  // Read packet from FIFO (while radio is in IDLE state)
  PacketBuffer pkt;
  uint8_t length;
  if (!this->read_packet_from_fifo_(pkt.data, length)) {
    return false;  // Invalid packet
  }

  // Store packet in buffer
  pkt.length = length + 1;
  pkt.timestamp = millis();
  pkt.valid = true;
  return this->packet_buffer_.push(pkt);
}

void Multical21WMBusComponent::process_buffered_packets_() {
  PacketBuffer pkt;
  while (this->packet_buffer_.pop(pkt)) {
    // Update last packet time
    this->last_packet_time_ = pkt.timestamp;

    // Process packet
    this->process_packet_(pkt.data, pkt.length);
  }
}

bool Multical21WMBusComponent::validate_packet_structure_(const uint8_t *packet_data, uint8_t length, uint8_t packet_length) {
  // Guard clause: check length validity
  if (length > MAX_PACKET_SIZE || length < MIN_WMBUS_PACKET_LENGTH) {
    ESP_LOGW(TAG, "Invalid packet length: %u", length);
    return false;
  }

  // Guard clause: check buffer size
  if (packet_length < length + 1) {
    ESP_LOGW(TAG, "Buffer too short: have %u bytes, need %u", packet_length, length + 1);
    return false;
  }

  return true;
}

bool Multical21WMBusComponent::verify_packet_crc_(const uint8_t *packet_data, uint8_t length) {
  uint16_t calculated_crc = WMBusCrypto::calculate_crc(packet_data, length - 1);
  uint16_t packet_crc = (packet_data[length - 1] << 8) | packet_data[length];

  if (calculated_crc != packet_crc) {
    ESP_LOGW(TAG, "CRC verification FAILED! calc=0x%04X, packet=0x%04X",
             calculated_crc, packet_crc);
    this->crc_errors_++;
    return false;
  }

  ESP_LOGI(TAG, "CRC verification PASSED (0x%04X)", calculated_crc);
  return true;
}

bool Multical21WMBusComponent::decrypt_packet_payload_(const uint8_t *packet_data, uint8_t length,
                                                        uint8_t *plaintext, uint8_t &plaintext_length) {
  // Convert vector to array for crypto API
  std::array<uint8_t, 16> aes_key_array;
  std::copy(this->aes_key_.begin(), this->aes_key_.end(), aes_key_array.begin());

  // Decrypt using crypto helper
  return this->crypto_.decrypt_packet(packet_data, length, aes_key_array, plaintext, plaintext_length);
}

// ============================================================================
// Main Loop
// ============================================================================

void Multical21WMBusComponent::loop() {
  // Guard clause: only process if interrupt fired
  if (!this->packet_ready_) {
    return;
  }

  // Detach interrupt during FIFO processing to prevent race conditions
  detachInterrupt(digitalPinToInterrupt(this->gdo0_pin_));

  // Clear flag and increment counter
  this->packet_ready_ = false;
  this->packets_received_++;

  // Process the packet
  if (!this->read_fifo_into_packet_buffer_()) {
    this->radio_.start_rx();
    // Re-attach interrupt before returning
    attachInterrupt(digitalPinToInterrupt(this->gdo0_pin_),
                    []() {
                      if (isr_instance_ != nullptr) {
                        Multical21WMBusComponent::packet_isr_(isr_instance_);
                      }
                    },
                    FALLING);
    return;
  }

  // Restart receiver for next packet
  this->radio_.start_rx();

  // Re-attach interrupt
  attachInterrupt(digitalPinToInterrupt(this->gdo0_pin_),
                  []() {
                    if (isr_instance_ != nullptr) {
                      Multical21WMBusComponent::packet_isr_(isr_instance_);
                    }
                  },
                  FALLING);

  // Process all packets in buffer
  this->process_buffered_packets_();
}

void Multical21WMBusComponent::update() {
  // Periodic update called based on polling interval
  // Print transmission interval statistics ONLY for configured meter

  uint32_t now = millis();

  // Look for OUR meter in stats
  bool found_our_meter = false;
  for (const auto &stats : this->meter_stats_) {
    // Convert meter ID back to bytes for display (big-endian format)
    uint8_t id_bytes[4] = {
      (uint8_t)((stats.meter_id >> 24) & 0xFF),
      (uint8_t)((stats.meter_id >> 16) & 0xFF),
      (uint8_t)((stats.meter_id >> 8) & 0xFF),
      (uint8_t)(stats.meter_id & 0xFF)
    };

    // Check if this is our meter
    // id_bytes is already in big-endian format, same as this->meter_id_
    bool is_ours = (id_bytes[0] == this->meter_id_[0] &&
                    id_bytes[1] == this->meter_id_[1] &&
                    id_bytes[2] == this->meter_id_[2] &&
                    id_bytes[3] == this->meter_id_[3]);

    if (!is_ours) {
      continue;  // Skip other meters
    }

    found_our_meter = true;

    // Calculate time since last packet
    uint32_t elapsed_ms = now - stats.last_seen_ms;
    uint32_t elapsed_sec = elapsed_ms / 1000;

    ESP_LOGI(TAG, "===========================================================");
    ESP_LOGI(TAG, "OUR METER: %02X%02X%02X%02X", id_bytes[0], id_bytes[1], id_bytes[2], id_bytes[3]);
    ESP_LOGI(TAG, "===========================================================");

    if (stats.packet_count > 1) {
      uint32_t avg_interval_ms = stats.total_interval_ms / (stats.packet_count - 1);
      uint32_t avg_interval_sec = avg_interval_ms / 1000;

      // Calculate estimated time until next packet
      int32_t time_until_next_sec = avg_interval_sec - elapsed_sec;

      ESP_LOGI(TAG, "  Packets received: %u", stats.packet_count);
      ESP_LOGI(TAG, "  Average interval: %u seconds", avg_interval_sec);
      ESP_LOGI(TAG, "  Last seen: %u seconds ago", elapsed_sec);

      // Frame type statistics
      ESP_LOGI(TAG, "  Frame types: compact=%u, long=%u, last=%s",
               stats.compact_frame_count, stats.long_frame_count, stats.last_frame_type.c_str());
      if (stats.compact_frame_count + stats.long_frame_count > 0) {
        float compact_ratio = (float)stats.compact_frame_count / (stats.compact_frame_count + stats.long_frame_count) * 100.0f;
        ESP_LOGI(TAG, "  Compact ratio: %.1f%% (expect ~87.5%% = 7/8)", compact_ratio);
      }

      if (time_until_next_sec > 0) {
        ESP_LOGI(TAG, "  Next packet expected in: ~%d seconds", time_until_next_sec);
      } else {
        ESP_LOGI(TAG, "  Next packet: OVERDUE by %d seconds", -time_until_next_sec);
      }
    } else {
      ESP_LOGI(TAG, "  Packets received: 1");
      ESP_LOGI(TAG, "  Last seen: %u seconds ago", elapsed_sec);
      ESP_LOGI(TAG, "  Frame type: %s", stats.last_frame_type.c_str());
      ESP_LOGI(TAG, "  (Need at least 2 packets to calculate interval)");
    }
    ESP_LOGI(TAG, "===========================================================");
    break;  // Found our meter, no need to continue
  }

  if (!found_our_meter) {
    // Display meter ID in the same order as printed on the physical meter
    ESP_LOGI(TAG, "Configured meter %02X%02X%02X%02X not detected yet",
             this->meter_id_[0], this->meter_id_[1], this->meter_id_[2], this->meter_id_[3]);
  }
}

void Multical21WMBusComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Multical21 wMBUS Receiver:");
  ESP_LOGCONFIG(TAG, "  GDO0 Pin: GPIO%u", this->gdo0_pin_);
  LOG_SENSOR("  ", "Total Consumption", this->total_consumption_sensor_);
  LOG_SENSOR("  ", "Target Consumption", this->target_consumption_sensor_);
  LOG_SENSOR("  ", "Flow Temperature", this->flow_temperature_sensor_);
  LOG_SENSOR("  ", "Ambient Temperature", this->ambient_temperature_sensor_);

  // Display meter ID in the same order as printed on the physical meter
  ESP_LOGCONFIG(TAG, "  Meter ID: %02X%02X%02X%02X",
                this->meter_id_[0], this->meter_id_[1], this->meter_id_[2], this->meter_id_[3]);
  ESP_LOGCONFIG(TAG, "  Statistics: Received=%u, Valid=%u, CRC Errors=%u, ID Mismatches=%u",
                this->packets_received_, this->packets_valid_, this->crc_errors_, this->id_mismatches_);
}

// ============================================================================
// Packet Processing
// ============================================================================

void IRAM_ATTR Multical21WMBusComponent::packet_isr_(Multical21WMBusComponent *instance) {
  // CRITICAL TIMING PATH - Minimal ISR: just set flag and wake loop
  // Per WMBUS_IMPLEMENTATION_SPEC.md Section 5.1:
  // - GDO0 falling edge = packet complete, data in FIFO
  // - Must read FIFO quickly before next packet arrives
  // - Use enable_loop_soon_any_context() to wake loop ASAP
  //
  // Design: ISR only sets flag, loop() reads FIFO immediately when woken

  instance->packet_ready_ = true;
  instance->enable_loop_soon_any_context();
}

void Multical21WMBusComponent::process_packet_(const uint8_t *packet_data,
                                                uint8_t packet_length) {
  uint8_t length = packet_data[0];

  // Guard clauses for validation
  if (!this->validate_packet_structure_(packet_data, length, packet_length)) {
    return;
  }

  // Check if it's our meter (guard clause)
  uint8_t meter_id[4] = {packet_data[4], packet_data[5], packet_data[6], packet_data[7]};
  if (!this->is_our_meter_id_(meter_id)) {
    return;  // Not our meter, skip silently
  }

  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "*** PROCESSING OUR METER ***");
  ESP_LOGI(TAG, "========================================");

  // Verify CRC (guard clause)
  if (!this->verify_packet_crc_(packet_data, length)) {
    return;
  }

  // Decrypt payload
  uint8_t plaintext[MAX_PACKET_SIZE];
  uint8_t plaintext_length;
  if (!this->decrypt_packet_payload_(packet_data, length, plaintext, plaintext_length)) {
    return;
  }

  // Parse meter data using parser helper
  WMBusMeterData data = this->parser_.parse(plaintext, plaintext_length);
  if (!data.valid) {
    ESP_LOGW(TAG, "Failed to parse meter data");
    return;
  }

  // Update statistics (now that we have frame_type from parsing)
  uint32_t meter_id_uint = (meter_id[3] << 24) | (meter_id[2] << 16) |
                           (meter_id[1] << 8) | meter_id[0];
  this->update_meter_stats_(meter_id_uint, data.frame_type);

  // Publish data to sensors
  this->publish_meter_data_(data);

  // Success!
  this->packets_valid_++;
  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "Packet processed successfully!");
  ESP_LOGI(TAG, "Total valid packets: %u", this->packets_valid_);
  ESP_LOGI(TAG, "========================================");
}

void Multical21WMBusComponent::publish_meter_data_(const WMBusMeterData &data) {
  // Publish to ESPHome sensors
  if (this->total_consumption_sensor_ != nullptr) {
    this->total_consumption_sensor_->publish_state(data.total_consumption_m3);
  }
  if (this->target_consumption_sensor_ != nullptr) {
    this->target_consumption_sensor_->publish_state(data.target_consumption_m3);
  }
  if (this->flow_temperature_sensor_ != nullptr) {
    this->flow_temperature_sensor_->publish_state(data.flow_temperature_c);
  }
  if (this->ambient_temperature_sensor_ != nullptr) {
    this->ambient_temperature_sensor_->publish_state(data.ambient_temperature_c);
  }
  if (this->info_codes_sensor_ != nullptr) {
    this->info_codes_sensor_->publish_state(data.status.c_str());
  }
  ESP_LOGI(TAG, "Meter data published to sensors");
}

// ============================================================================
// Health Monitoring
// ============================================================================

void Multical21WMBusComponent::log_radio_status_() {
  // Just log status for diagnostics, don't process packets
  uint8_t marcstate = this->radio_.get_marcstate();
  uint8_t rxbytes = this->radio_.get_rx_bytes();
  uint8_t num_bytes = rxbytes & 0x7F;
  bool overflow = rxbytes & 0x80;

  // Read RSSI for signal strength
  uint8_t rssi_raw = this->radio_.read_status_register(0x34);  // RSSI register
  int16_t rssi_dbm;
  if (rssi_raw >= 128) {
    rssi_dbm = (rssi_raw - 256) / 2 - 74;
  } else {
    rssi_dbm = rssi_raw / 2 - 74;
  }

  ESP_LOGD(TAG, "Radio status: MARC=0x%02X, RXbytes=%u, overflow=%s, interrupts=%u, ready=%s, RSSI=%ddBm",
           marcstate, num_bytes, overflow ? "YES" : "no",
           this->packets_received_, this->packet_ready_ ? "YES" : "no", rssi_dbm);

  // Check if radio is in wrong state or overflow
  if (marcstate == MARCSTATE_RXFIFO_OVERFLOW || overflow) {
    ESP_LOGW(TAG, "Radio in OVERFLOW state (MARC=0x%02X, overflow=%s) - restarting",
             marcstate, overflow ? "YES" : "no");
    this->radio_.enter_idle();
    this->radio_.flush_rx_fifo();
    this->radio_.start_rx();
  } else if (marcstate != MARCSTATE_RX) {
    ESP_LOGW(TAG, "Radio not in RX mode (state=0x%02X, expected 0x%02X) - restarting",
             marcstate, MARCSTATE_RX);
    this->radio_.start_rx();
  }
}

}  // namespace multical21_wmbus
}  // namespace esphome
