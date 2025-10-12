#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/spi/spi.h"
#include "wmbus_types.h"
#include "cc1101_radio.h"
#include "wmbus_crypto.h"
#include "wmbus_packet_parser.h"
#include "wmbus_packet_buffer.h"

namespace esphome {
namespace multical21_wmbus {

static const char *const TAG = "multical21_wmbus";

class Multical21WMBusComponent : public PollingComponent, public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST,
                                                                                  spi::CLOCK_POLARITY_LOW,
                                                                                  spi::CLOCK_PHASE_LEADING,
                                                                                  spi::DATA_RATE_4MHZ> {
 public:
  Multical21WMBusComponent() = default;

  // Component lifecycle methods
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // Configuration setters
  void set_meter_id(const std::vector<uint8_t> &meter_id) { this->meter_id_ = meter_id; }
  void set_aes_key(const std::vector<uint8_t> &aes_key) { this->aes_key_ = aes_key; }
  void set_gdo0_pin(uint8_t pin) { this->gdo0_pin_ = pin; }

  // Sensor setters
  void set_total_consumption_sensor(sensor::Sensor *sensor) { this->total_consumption_sensor_ = sensor; }
  void set_target_consumption_sensor(sensor::Sensor *sensor) { this->target_consumption_sensor_ = sensor; }
  void set_flow_temperature_sensor(sensor::Sensor *sensor) { this->flow_temperature_sensor_ = sensor; }
  void set_ambient_temperature_sensor(sensor::Sensor *sensor) { this->ambient_temperature_sensor_ = sensor; }
  void set_info_codes_sensor(text_sensor::TextSensor *sensor) { this->info_codes_sensor_ = sensor; }

 protected:
  // High-level packet processing (coordinates helper classes)
  void process_packet_(const uint8_t *packet_data, uint8_t packet_length);
  void publish_meter_data_(const WMBusMeterData &data);

  // Helper functions
  void update_meter_stats_(uint32_t meter_id_uint, const std::string &frame_type);
  bool is_our_meter_id_(const uint8_t *meter_id_le);
  bool read_packet_from_fifo_(uint8_t *buffer, uint8_t &length);
  bool read_fifo_into_packet_buffer_();
  void process_buffered_packets_();
  bool validate_packet_structure_(const uint8_t *packet_data, uint8_t length, uint8_t packet_length);
  bool verify_packet_crc_(const uint8_t *packet_data, uint8_t length);
  bool decrypt_packet_payload_(const uint8_t *packet_data, uint8_t length,
                                uint8_t *plaintext, uint8_t &plaintext_length);

  // Health monitoring
  void log_radio_status_();

  // Interrupt handling - CRITICAL TIMING PATH
  static void IRAM_ATTR packet_isr_(Multical21WMBusComponent *instance);
  static Multical21WMBusComponent *isr_instance_;
  volatile bool packet_ready_{false};

  // Helper classes (composition)
  CC1101Radio radio_;
  WMBusCrypto crypto_;
  WMBusPacketParser parser_;
  WMBusPacketBuffer<4> packet_buffer_;

  // Configuration
  std::vector<uint8_t> meter_id_;
  std::vector<uint8_t> aes_key_;
  uint8_t gdo0_pin_;

  // Sensors
  sensor::Sensor *total_consumption_sensor_{nullptr};
  sensor::Sensor *target_consumption_sensor_{nullptr};
  sensor::Sensor *flow_temperature_sensor_{nullptr};
  sensor::Sensor *ambient_temperature_sensor_{nullptr};
  text_sensor::TextSensor *info_codes_sensor_{nullptr};

  // State tracking
  uint32_t last_packet_time_{0};
  uint32_t last_health_check_{0};
  uint32_t packets_received_{0};
  uint32_t packets_valid_{0};
  uint32_t crc_errors_{0};
  uint32_t id_mismatches_{0};

  // Meter transmission tracking (for analyzing transmission intervals)
  std::vector<MeterStats> meter_stats_;
};

}  // namespace multical21_wmbus
}  // namespace esphome
