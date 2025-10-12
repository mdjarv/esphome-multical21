#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace esphome {
namespace multical21_wmbus {

// ============================================================================
// CC1101 Register Addresses
// ============================================================================

constexpr uint8_t CC1101_IOCFG2 = 0x00;
constexpr uint8_t CC1101_IOCFG0 = 0x02;
constexpr uint8_t CC1101_FIFOTHR = 0x03;
constexpr uint8_t CC1101_PKTCTRL0 = 0x08;
constexpr uint8_t CC1101_FREQ2 = 0x0D;
constexpr uint8_t CC1101_FREQ1 = 0x0E;
constexpr uint8_t CC1101_FREQ0 = 0x0F;
constexpr uint8_t CC1101_MDMCFG4 = 0x10;
constexpr uint8_t CC1101_MDMCFG3 = 0x11;
constexpr uint8_t CC1101_MDMCFG2 = 0x12;
constexpr uint8_t CC1101_DEVIATN = 0x15;
constexpr uint8_t CC1101_MCSM1 = 0x17;
constexpr uint8_t CC1101_MCSM0 = 0x18;

// ============================================================================
// CC1101 Command Strobes
// ============================================================================

constexpr uint8_t CC1101_SRES = 0x30;   // Reset chip
constexpr uint8_t CC1101_SCAL = 0x33;   // Calibrate frequency synthesizer
constexpr uint8_t CC1101_SRX = 0x34;    // Enable RX
constexpr uint8_t CC1101_SIDLE = 0x36;  // Exit RX/TX
constexpr uint8_t CC1101_SFRX = 0x3A;   // Flush RX FIFO
constexpr uint8_t CC1101_RXFIFO = 0x3F; // RX FIFO access

// ============================================================================
// CC1101 Status Registers
// ============================================================================

constexpr uint8_t CC1101_MARCSTATE = 0x35;  // Main radio control state
constexpr uint8_t CC1101_RSSI = 0x34;       // RSSI value
constexpr uint8_t CC1101_RXBYTES = 0x3B;    // RX FIFO bytes

// ============================================================================
// MARCSTATE Values (from CC1101 datasheet Table 31)
// ============================================================================

constexpr uint8_t MARCSTATE_IDLE = 0x01;
constexpr uint8_t MARCSTATE_RX = 0x0F;             // Receiving (correct value)
constexpr uint8_t MARCSTATE_RXFIFO_OVERFLOW = 0x11;
constexpr uint8_t MARCSTATE_RX_OVERFLOW = 0x0D;    // RX overflow state

// ============================================================================
// Read/Write Masks for Register Access
// ============================================================================

constexpr uint8_t CC1101_WRITE_SINGLE = 0x00;
constexpr uint8_t CC1101_WRITE_BURST = 0x40;
constexpr uint8_t CC1101_READ_SINGLE = 0x80;
constexpr uint8_t CC1101_READ_BURST = 0xC0;

// ============================================================================
// wMBUS Packet Constants
// ============================================================================

constexpr uint8_t MAX_PACKET_SIZE = 64;
constexpr uint8_t HEADER_SIZE = 16;
constexpr uint8_t CRC_SIZE = 2;
constexpr uint16_t CRC_POLY = 0x3D65;

// ============================================================================
// Timeout Constants
// ============================================================================

constexpr uint32_t RECEIVE_TIMEOUT_MS = 300000;  // 5 minutes
constexpr uint32_t HEALTH_CHECK_INTERVAL_MS = 10000;  // 10 seconds

// ============================================================================
// wMBUS Packet Size Constraints
// ============================================================================

constexpr uint8_t MIN_WMBUS_PACKET_LENGTH = 10;
constexpr uint8_t WMBUS_HEADER_SIZE = 18;  // L(1) + header(16) + CRC offset(1)
constexpr uint8_t MAX_FIFO_READ_BYTES = 70;  // Safety limit for FIFO reads
constexpr uint8_t INVALID_LENGTH_MARKER = 255;

// ============================================================================
// Packet Structure Offsets
// ============================================================================

constexpr uint8_t OFFSET_C_FIELD = 1;
constexpr uint8_t OFFSET_M_FIELD = 2;
constexpr uint8_t OFFSET_METER_ID = 4;
constexpr uint8_t OFFSET_CIPHER_START = 17;

// ============================================================================
// Packet Ring Buffer Configuration
// ============================================================================

constexpr uint8_t PACKET_RING_SIZE = 4;  // Handle burst of 4 packets

// ============================================================================
// Shared Data Structures
// ============================================================================

/**
 * @brief Packet buffer structure for ISR-to-loop communication
 */
struct PacketBuffer {
  uint8_t data[MAX_PACKET_SIZE + 1];  // L-field + payload
  uint8_t length;
  uint32_t timestamp;
  bool valid;
};

/**
 * @brief Meter statistics for tracking transmission intervals
 */
struct MeterStats {
  uint32_t meter_id;
  uint32_t last_seen_ms;
  uint32_t packet_count;
  uint32_t total_interval_ms;  // Sum of all intervals for averaging

  // Frame type analysis
  uint32_t compact_frame_count;
  uint32_t long_frame_count;
  std::string last_frame_type;  // "compact" or "long"
};

}  // namespace multical21_wmbus
}  // namespace esphome
