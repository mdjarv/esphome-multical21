#include "cc1101_radio.h"
#include "multical21_wmbus.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace multical21_wmbus {

static const char *const RADIO_TAG = "multical21_wmbus.radio";

// ============================================================================
// CC1101 Register Configuration for wMBUS Mode C (868.95 MHz)
// ============================================================================

struct CC1101Config {
  uint8_t reg;
  uint8_t value;
};

static const CC1101Config CC1101_REGISTERS[] = {
    {0x00, 0x2E},  // IOCFG2: GDO2 high impedance
    {0x02, 0x06},  // IOCFG0: GDO0 asserts on sync word, deasserts at end of packet
    {0x03, 0x00},  // FIFOTHR: RX FIFO threshold
    {0x04, 0x54},  // SYNC1: Sync word high byte
    {0x05, 0x3D},  // SYNC0: Sync word low byte (wMBUS Mode C: 0x543D)
    {0x06, 0x30},  // PKTLEN: Max packet length (48 bytes)
    {0x07, 0x00},  // PKTCTRL1: No address check, no CRC autoflush
    {0x08, 0x02},  // PKTCTRL0: Infinite packet length mode (PKTCTRL0[1:0]=10)
    {0x09, 0x00},  // ADDR: Device address (unused)
    {0x0A, 0x00},  // CHANNR: Channel number
    {0x0B, 0x08},  // FSCTRL1: IF frequency
    {0x0C, 0x00},  // FSCTRL0: Frequency offset
    {0x0D, 0x21},  // FREQ2: Frequency control word, high byte
    {0x0E, 0x6B},  // FREQ1: Frequency control word, middle byte
    {0x0F, 0xD0},  // FREQ0: Frequency control word, low byte (868.95 MHz)
    {0x10, 0x5C},  // MDMCFG4: Channel bandwidth & data rate exponent
    {0x11, 0x04},  // MDMCFG3: Data rate mantissa (100 kbps)
    {0x12, 0x06},  // MDMCFG2: 2-FSK modulation, 15/16 sync word bits
    {0x13, 0x22},  // MDMCFG1: FEC disabled, preamble bytes = 4
    {0x14, 0xF8},  // MDMCFG0: Channel spacing mantissa
    {0x15, 0x44},  // DEVIATN: Deviation ±50 kHz
    {0x17, 0x00},  // MCSM1: Stay in IDLE after RX/TX
    {0x18, 0x18},  // MCSM0: Auto-calibrate when going from IDLE to RX/TX
    {0x19, 0x2E},  // FOCCFG: Frequency offset compensation
    {0x1A, 0xBF},  // BSCFG: Bit synchronization
    {0x1B, 0x43},  // AGCCTRL2: AGC control
    {0x1C, 0x09},  // AGCCTRL1: AGC control
    {0x1D, 0xB5},  // AGCCTRL0: AGC filter, wait time
    {0x21, 0xB6},  // FREND1: Front end RX configuration
    {0x22, 0x10},  // FREND0: Front end TX configuration
    {0x23, 0xEA},  // FSCAL3: Frequency synthesizer calibration
    {0x24, 0x2A},  // FSCAL2: Frequency synthesizer calibration
    {0x25, 0x00},  // FSCAL1: Frequency synthesizer calibration
    {0x26, 0x1F},  // FSCAL0: Frequency synthesizer calibration
    {0x29, 0x59},  // FSTEST: Frequency synthesizer test
    {0x2C, 0x81},  // TEST2: Various test settings
    {0x2D, 0x35},  // TEST1: Various test settings
    {0x2E, 0x09},  // TEST0: Various test settings
};

// ============================================================================
// Initialization
// ============================================================================

void CC1101Radio::init(Multical21WMBusComponent *component) {
  this->component_ = component;
  ESP_LOGD(RADIO_TAG, "CC1101Radio initialized with parent component");
}

// ============================================================================
// Private Helper Methods
// ============================================================================

void CC1101Radio::wait_for_miso_low_() {
  // ESPHome SPI driver handles MISO state internally
  // Unlike the reference implementation which uses raw SPI.transfer(),
  // ESPHome's spi->read_byte() already waits for chip ready
  //
  // Attempting to digitalRead(MISO) when it's configured for SPI causes errors:
  // "IO 5 is not set as GPIO"
  //
  // Solution: Just add a small delay to ensure chip is ready
  delayMicroseconds(10);
}

void CC1101Radio::send_strobe_(uint8_t strobe) {
  this->component_->enable();
  delayMicroseconds(5);
  this->wait_for_miso_low_();
  this->component_->write_byte(strobe);
  delayMicroseconds(5);
  this->component_->disable();
}

// ============================================================================
// Public Hardware Interface
// ============================================================================

void CC1101Radio::reset() {
  ESP_LOGD(RADIO_TAG, "Resetting CC1101...");

  // Software reset - simpler and works with ESPHome's SPI abstraction
  // The hardware reset sequence requires direct pin manipulation which
  // conflicts with ESPHome's SPI driver
  this->send_strobe_(CC1101_SRES);
  delay(10);  // Give chip time to reset

  ESP_LOGD(RADIO_TAG, "CC1101 reset complete");
}

void CC1101Radio::configure() {
  ESP_LOGD(RADIO_TAG, "Configuring CC1101 registers...");

  // Check if CC1101 is responding by reading VERSION register
  uint8_t version = this->read_status_register(0x31);  // VERSION register
  uint8_t partnum = this->read_status_register(0x30);  // PARTNUM register
  ESP_LOGCONFIG(RADIO_TAG, "CC1101 PARTNUM=0x%02X, VERSION=0x%02X (expected PARTNUM=0x00, VERSION=0x04 or 0x14)",
                partnum, version);

  // Write all configuration registers
  for (const auto &config : CC1101_REGISTERS) {
    this->write_register(config.reg, config.value);
  }

  // Read back a few key registers to verify write
  uint8_t freq2 = this->read_register(0x0D);
  uint8_t mdmcfg2 = this->read_register(0x12);
  ESP_LOGD(RADIO_TAG, "Verify: FREQ2=0x%02X (expect 0x21), MDMCFG2=0x%02X (expect 0x06)", freq2, mdmcfg2);

  // Calibrate
  this->send_strobe_(CC1101_SCAL);
  delay(1);

  ESP_LOGD(RADIO_TAG, "CC1101 configuration complete");
}

void CC1101Radio::start_rx() {
  // Note: This is called frequently (after every packet), so we keep logging minimal
  // Only log errors, not normal operation

  // Check current state before trying to change it
  uint8_t current_state = this->read_status_register(CC1101_MARCSTATE);

  // If already in overflow state (0x11), handle it specially
  if (current_state == MARCSTATE_RXFIFO_OVERFLOW) {
    ESP_LOGW(RADIO_TAG, "Radio in OVERFLOW state (0x11), performing IDLE->FLUSH->RX sequence");
    // Enter IDLE first to clear overflow condition
    this->send_strobe_(CC1101_SIDLE);
    delay(2);
    // Flush both RX and TX FIFOs
    this->send_strobe_(CC1101_SFRX);
    delay(1);
    this->send_strobe_(CC1101_SFTX);
    delay(1);
    // Now can safely enter RX
    this->send_strobe_(CC1101_SRX);
    delay(10);

    uint8_t final_state = this->read_status_register(CC1101_MARCSTATE);
    if (final_state != MARCSTATE_RX) {
      ESP_LOGE(RADIO_TAG, "Failed to recover from overflow! State=0x%02X", final_state);
      this->reset();
      this->configure();
    }
    return;
  }

  // Enter IDLE state
  this->send_strobe_(CC1101_SIDLE);

  // Wait for IDLE state (matching working code logic)
  uint8_t regCount = 0;
  while (this->read_status_register(CC1101_MARCSTATE) != MARCSTATE_IDLE) {
    if (regCount++ > 100) {
      uint8_t stuck_state = this->read_status_register(CC1101_MARCSTATE);
      ESP_LOGE(RADIO_TAG, "Failed to enter IDLE state! Stuck in state 0x%02X (was 0x%02X)", stuck_state, current_state);
      // Reset and try again
      this->reset();
      this->configure();
      return;
    }
    delay(1);
  }

  // Flush RX FIFO - CRITICAL: Must wait for flush to complete!
  this->send_strobe_(CC1101_SFRX);
  delay(5);  // Give FIFO time to flush completely

  // Verify FIFO is actually empty
  uint8_t rxbytes_after_flush = this->read_status_register(CC1101_RXBYTES);
  if ((rxbytes_after_flush & 0x7F) != 0) {
    ESP_LOGW(RADIO_TAG, "FIFO not empty after flush! RXbytes=%u, attempting second flush", rxbytes_after_flush & 0x7F);
    this->send_strobe_(CC1101_SFRX);
    delay(5);
  }

  // Enter RX state
  regCount = 0;
  this->send_strobe_(CC1101_SRX);
  delay(10);  // Give time to enter RX mode

  // Wait for RX state with timeout
  while (this->read_status_register(CC1101_MARCSTATE) != MARCSTATE_RX) {
    if (regCount++ > 100) {
      uint8_t stuck_state = this->read_status_register(CC1101_MARCSTATE);
      ESP_LOGE(RADIO_TAG, "Failed to enter RX state! Stuck in state 0x%02X", stuck_state);

      // Check if it's an overflow condition
      if (stuck_state == MARCSTATE_RXFIFO_OVERFLOW) {
        ESP_LOGW(RADIO_TAG, "Detected overflow (0x11) while entering RX - need full reset");
      }

      // Reset and try again
      this->reset();
      this->configure();
      return;
    }
    delay(1);
  }

  // No logging here - this runs after every packet, would spam logs
}

void CC1101Radio::enter_idle() {
  this->send_strobe_(CC1101_SIDLE);
  delay(2);
}

void CC1101Radio::flush_rx_fifo() {
  this->send_strobe_(CC1101_SFRX);
}

void CC1101Radio::write_register(uint8_t reg, uint8_t value) {
  this->component_->enable();
  this->wait_for_miso_low_();
  this->component_->write_byte(reg);
  this->component_->write_byte(value);
  this->component_->disable();
}

uint8_t CC1101Radio::read_register(uint8_t reg) {
  this->component_->enable();
  this->wait_for_miso_low_();
  this->component_->write_byte(reg | CC1101_READ_SINGLE);
  uint8_t value = this->component_->read_byte();
  this->component_->disable();
  return value;
}

uint8_t CC1101Radio::read_status_register(uint8_t reg) {
  this->component_->enable();
  this->wait_for_miso_low_();
  this->component_->write_byte(reg | CC1101_READ_BURST);
  uint8_t value = this->component_->read_byte();
  this->component_->disable();
  return value;
}

uint8_t CC1101Radio::read_fifo_byte() {
  this->component_->enable();
  this->wait_for_miso_low_();
  this->component_->write_byte(CC1101_RXFIFO | CC1101_READ_SINGLE);
  uint8_t value = this->component_->read_byte();
  this->component_->disable();
  return value;
}

uint8_t CC1101Radio::get_rx_bytes() {
  return this->read_status_register(CC1101_RXBYTES);
}

uint8_t CC1101Radio::get_marcstate() {
  return this->read_status_register(CC1101_MARCSTATE);
}

bool CC1101Radio::is_overflow() {
  uint8_t rxbytes = this->get_rx_bytes();
  return (rxbytes & 0x80) != 0;  // Bit 7 indicates overflow
}

}  // namespace multical21_wmbus
}  // namespace esphome
