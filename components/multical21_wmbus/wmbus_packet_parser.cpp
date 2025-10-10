#include "wmbus_packet_parser.h"
#include "esphome/core/log.h"
#include <cstdio>

namespace esphome {
namespace multical21_wmbus {

static const char *const TAG = "multical21_wmbus.parser";

// ============================================================================
// Private Helper Methods
// ============================================================================

bool WMBusPacketParser::is_long_frame_(const uint8_t *plaintext) {
  // Long frame marker is 0x78 at byte 2 (per Multical21 spec)
  return (plaintext[2] == 0x78);
}

std::string WMBusPacketParser::decode_status_(uint8_t info_codes) {
  switch (info_codes) {
    case 0x00:
      return "normal";
    case 0x01:
      return "dry";
    case 0x02:
      return "reverse";
    case 0x04:
      return "leak";
    case 0x08:
      return "burst";
    default:
      {
        char buf[16];
        snprintf(buf, sizeof(buf), "code_0x%02x", info_codes);
        return std::string(buf);
      }
  }
}

// ============================================================================
// Public Parsing Method
// ============================================================================

WMBusMeterData WMBusPacketParser::parse(const uint8_t *plaintext, uint8_t length) {
  WMBusMeterData data;
  data.valid = false;

  // Minimum length check
  if (plaintext == nullptr || length < 10) {
    ESP_LOGW(TAG, "Plaintext too short for parsing: %u bytes", length);
    return data;
  }

  // Store plaintext length for analysis
  data.plaintext_length = length;

  // Detect frame type (compact vs long)
  bool is_long = this->is_long_frame_(plaintext);
  data.frame_type = is_long ? "long" : "compact";
  data.frame_marker = (length >= 3) ? plaintext[2] : 0x00;

  // Define field positions based on frame type
  uint8_t pos_info_codes, pos_total, pos_target, pos_flow_temp, pos_ambient_temp;

  if (is_long) {
    // Long frame field positions
    pos_info_codes = 6;
    pos_total = 10;
    pos_target = 16;
    pos_flow_temp = 22;
    pos_ambient_temp = 25;
  } else {
    // Compact frame field positions
    pos_info_codes = 7;
    pos_total = 9;
    pos_target = 13;
    pos_flow_temp = 17;
    pos_ambient_temp = 18;
  }

  ESP_LOGI(TAG, ">>> Frame Type: %s (marker=0x%02X, length=%u bytes) <<<",
           data.frame_type.c_str(), data.frame_marker, length);

  // Log first 30 bytes of plaintext in hex for analysis
  if (length > 0) {
    char hex_buf[100];
    int offset = 0;
    for (int i = 0; i < length && i < 30 && offset < 90; i++) {
      offset += snprintf(hex_buf + offset, sizeof(hex_buf) - offset, "%02X ", plaintext[i]);
    }
    ESP_LOGD(TAG, "Plaintext hex: %s%s", hex_buf, (length > 30) ? "..." : "");
  }

  // Extract meter status / info codes
  if (length > pos_info_codes) {
    uint8_t info_codes = plaintext[pos_info_codes];
    data.status = this->decode_status_(info_codes);
    ESP_LOGD(TAG, "  Status: %s (0x%02X)", data.status.c_str(), info_codes);
  }

  // Extract total water consumption (4 bytes, little-endian, in liters)
  if (length > pos_total + 3) {
    uint32_t total_liters = plaintext[pos_total] |
                            (plaintext[pos_total + 1] << 8) |
                            (plaintext[pos_total + 2] << 16) |
                            (plaintext[pos_total + 3] << 24);
    data.total_consumption_m3 = total_liters / 1000.0f;
    ESP_LOGD(TAG, "  Total consumption: %.3f m3", data.total_consumption_m3);
  }

  // Extract target water consumption (4 bytes, little-endian, in liters)
  if (length > pos_target + 3) {
    uint32_t target_liters = plaintext[pos_target] |
                             (plaintext[pos_target + 1] << 8) |
                             (plaintext[pos_target + 2] << 16) |
                             (plaintext[pos_target + 3] << 24);
    data.target_consumption_m3 = target_liters / 1000.0f;
    ESP_LOGD(TAG, "  Target consumption: %.3f m3", data.target_consumption_m3);
  }

  // Extract flow temperature (signed byte, degrees Celsius)
  if (length > pos_flow_temp) {
    data.flow_temperature_c = static_cast<int8_t>(plaintext[pos_flow_temp]);
    ESP_LOGD(TAG, "  Flow temperature: %d °C", data.flow_temperature_c);
  }

  // Extract ambient temperature (signed byte, degrees Celsius)
  if (length > pos_ambient_temp) {
    data.ambient_temperature_c = static_cast<int8_t>(plaintext[pos_ambient_temp]);
    ESP_LOGD(TAG, "  Ambient temperature: %d °C", data.ambient_temperature_c);
  }

  // Mark as valid if we successfully parsed
  data.valid = true;
  ESP_LOGI(TAG, "Parsing complete: %.3f m3, status=%s, flow=%d°C, ambient=%d°C",
           data.total_consumption_m3, data.status.c_str(),
           data.flow_temperature_c, data.ambient_temperature_c);

  return data;
}

}  // namespace multical21_wmbus
}  // namespace esphome
