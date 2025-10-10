#pragma once

#include "wmbus_types.h"

namespace esphome {
namespace multical21_wmbus {

// Forward declaration to avoid circular dependency
class Multical21WMBusComponent;

/**
 * @brief CC1101 radio hardware abstraction layer
 *
 * Complete encapsulation of CC1101 SPI hardware interface for wMBUS Mode C reception.
 * Handles initialization, configuration, state management, and FIFO operations.
 *
 * Responsibility: Pure hardware abstraction - no packet processing or crypto.
 * Extracted from: multical21_wmbus.cpp lines 351-482 (hardware interface section)
 */
class CC1101Radio {
 public:
  /**
   * @brief Initialize radio with parent component reference
   *
   * Must be called before any other operations.
   *
   * @param component Pointer to parent Multical21WMBusComponent for SPI access
   */
  void init(Multical21WMBusComponent *component);

  /**
   * @brief Reset CC1101 chip via software command
   *
   * Sends SRES strobe command and waits for chip to stabilize.
   */
  void reset();

  /**
   * @brief Configure CC1101 registers for wMBUS Mode C reception
   *
   * Writes all required register values for 868.95 MHz, 100 kbps, 2-FSK modulation.
   * Performs calibration after configuration.
   */
  void configure();

  /**
   * @brief Start receiver (enter RX mode)
   *
   * Sequence: IDLE → flush FIFO → RX mode
   * Called after every packet reception and during initialization.
   */
  void start_rx();

  /**
   * @brief Enter IDLE state
   *
   * Stops reception and allows safe register/FIFO access.
   */
  void enter_idle();

  /**
   * @brief Flush RX FIFO buffer
   *
   * Clears any stale data in the FIFO.
   */
  void flush_rx_fifo();

  /**
   * @brief Write to CC1101 configuration register
   *
   * @param reg Register address (0x00-0x2E)
   * @param value Byte value to write
   */
  void write_register(uint8_t reg, uint8_t value);

  /**
   * @brief Read from CC1101 configuration register
   *
   * @param reg Register address (0x00-0x2E)
   * @return Current register value
   */
  uint8_t read_register(uint8_t reg);

  /**
   * @brief Read CC1101 status register
   *
   * Status registers include MARCSTATE, RXBYTES, etc.
   *
   * @param reg Status register address (0x30-0x3D)
   * @return Current status value
   */
  uint8_t read_status_register(uint8_t reg);

  /**
   * @brief Read single byte from RX FIFO
   *
   * Must be called while in IDLE state.
   *
   * @return Byte from FIFO
   */
  uint8_t read_fifo_byte();

  /**
   * @brief Get number of bytes in RX FIFO
   *
   * Reads RXBYTES status register.
   *
   * @return Bits [6:0] = byte count, bit [7] = overflow flag
   */
  uint8_t get_rx_bytes();

  /**
   * @brief Get current MARCSTATE
   *
   * @return Current state (e.g., MARCSTATE_IDLE, MARCSTATE_RX)
   */
  uint8_t get_marcstate();

  /**
   * @brief Check if RX FIFO overflow occurred
   *
   * @return true if overflow bit set in RXBYTES register
   */
  bool is_overflow();

 private:
  Multical21WMBusComponent *component_{nullptr};

  /**
   * @brief Send command strobe to CC1101
   *
   * @param strobe Strobe command (e.g., CC1101_SRES, CC1101_SRX)
   */
  void send_strobe_(uint8_t strobe);

  /**
   * @brief Wait for MISO line to go low (chip ready indicator)
   *
   * In practice, adds a small delay for chip to become ready.
   */
  void wait_for_miso_low_();
};

}  // namespace multical21_wmbus
}  // namespace esphome
