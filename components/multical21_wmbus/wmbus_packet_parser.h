#pragma once

#include "wmbus_types.h"
#include <cstdint>
#include <string>

namespace esphome {
namespace multical21_wmbus {

/**
 * @brief Parsed meter data structure
 *
 * Data Transfer Object (DTO) holding all meter readings extracted
 * from a decrypted wMBUS packet.
 */
struct WMBusMeterData {
  float total_consumption_m3;    // Total water consumption in cubic meters
  float target_consumption_m3;   // Target/billing consumption in cubic meters
  int8_t flow_temperature_c;     // Flow temperature in degrees Celsius
  int8_t ambient_temperature_c;  // Ambient temperature in degrees Celsius
  std::string status;            // Human-readable meter status (e.g., "normal", "leak")
  bool valid;                    // True if parsing succeeded, false on error

  // Frame analysis fields
  std::string frame_type;        // "compact" or "long" - for debugging/analysis
  uint8_t plaintext_length;      // Length of decrypted plaintext in bytes
  uint8_t frame_marker;          // Byte 2 of plaintext (0x78 = long, other = compact)

  // Constructor with default invalid state
  WMBusMeterData() :
    total_consumption_m3(0.0f),
    target_consumption_m3(0.0f),
    flow_temperature_c(0),
    ambient_temperature_c(0),
    status("unknown"),
    valid(false),
    frame_type("unknown"),
    plaintext_length(0),
    frame_marker(0x00) {}
};

/**
 * @brief Parser for Multical21 wMBUS packet payloads
 *
 * Extracts meter readings from decrypted wMBUS data packets.
 * Supports both compact and long frame formats.
 *
 * Responsibility: Pure data extraction - no hardware, crypto, or ESPHome dependencies.
 * Extracted from: multical21_wmbus.cpp lines 615-698
 */
class WMBusPacketParser {
 public:
  /**
   * @brief Parse decrypted plaintext into meter readings
   *
   * Detects frame type (compact vs long) and extracts all meter data fields.
   * Handles variable field positions based on frame type.
   *
   * @param plaintext Decrypted payload data
   * @param length Length of plaintext in bytes
   * @return WMBusMeterData structure with parsed values (check .valid flag)
   */
  WMBusMeterData parse(const uint8_t *plaintext, uint8_t length);

 private:
  /**
   * @brief Detect if plaintext is a long frame format
   *
   * Long frames have different field positions than compact frames.
   *
   * @param plaintext Decrypted payload data
   * @return true if long frame (0x78 marker), false if compact frame
   */
  bool is_long_frame_(const uint8_t *plaintext);

  /**
   * @brief Decode meter status code to human-readable string
   *
   * Maps info code byte to descriptive status strings:
   * 0x00 = "normal", 0x01 = "dry", 0x02 = "reverse", etc.
   *
   * @param info_codes Raw status byte from meter
   * @return Status string (e.g., "normal", "leak", "code_0x05")
   */
  std::string decode_status_(uint8_t info_codes);
};

}  // namespace multical21_wmbus
}  // namespace esphome
