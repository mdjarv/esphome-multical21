#include "wmbus_crypto.h"
#include "esphome/core/log.h"
#include <mbedtls/aes.h>
#include <cstring>

namespace esphome {
namespace multical21_wmbus {

static const char *const TAG = "multical21_wmbus.crypto";

// ============================================================================
// CRC Calculation
// ============================================================================

uint16_t WMBusCrypto::calculate_crc(const uint8_t *data, uint8_t length) {
  // CRC-16-EN-13757-4 implementation per spec section 6.1
  const uint16_t poly = CRC_POLY;
  uint16_t crc = 0x0000;

  // Initial CRC calculation
  for (int i = 0; i < 16; i++) {
    bool bit = crc & 1;
    if (bit) {
      crc ^= poly;
    }
    crc >>= 1;
    if (bit) {
      crc |= 0x8000;
    }
  }

  // Process each byte
  for (uint8_t i = 0; i < length; i++) {
    uint8_t c = data[i];

    for (int j = 7; j >= 0; j--) {
      bool bit = crc & 0x8000;
      crc <<= 1;
      if (c & (1 << j)) {
        crc |= 1;
      }
      if (bit) {
        crc ^= poly;
      }
    }
  }

  // Final CRC calculation
  for (int i = 0; i < 16; i++) {
    bool bit = crc & 0x8000;
    crc <<= 1;
    if (bit) {
      crc ^= poly;
    }
  }

  // Final XOR
  crc ^= 0xFFFF;

  return crc & 0xFFFF;
}

// ============================================================================
// Decryption
// ============================================================================

void WMBusCrypto::build_iv_(const uint8_t *packet, uint8_t *iv) {
  // Build IV per EN 13757-4 Section 7.2
  // packet format: [L-field][C-field][M-field(2)][A-field(4)][...rest...]

  memset(iv, 0, 16);

  // Bytes 0-7: M-field + A-field
  // packet[2-3] = M-field (2 bytes)
  // packet[4-7] = A-field (4 bytes, meter ID)
  memcpy(iv, &packet[2], 8);

  // Byte 8: CI-field
  iv[8] = packet[11];

  // Bytes 9-12: Access number + Status + Configuration
  memcpy(&iv[9], &packet[13], 4);

  // Bytes 13-15: Padding (already zero from memset)
}

bool WMBusCrypto::decrypt_packet(const uint8_t *packet,
                                  uint8_t packet_length,
                                  const std::array<uint8_t, 16> &aes_key,
                                  uint8_t *plaintext,
                                  uint8_t &plaintext_length) {
  // Calculate cipher length
  // Cipher spans from byte 17 to byte (packet_length - 2) inclusive
  // Length = (packet_length - 2) - 17 + 1 = packet_length - 18
  plaintext_length = packet_length - CRC_SIZE - OFFSET_CIPHER_START + 1;

  if (plaintext_length < 1) {
    ESP_LOGW(TAG, "No encrypted data in packet");
    return false;
  }

  // Extract cipher data (starts at byte 17, after header)
  const uint8_t *cipher_data = &packet[OFFSET_CIPHER_START];

  // Build IV from packet header
  uint8_t iv[16];
  this->build_iv_(packet, iv);

  // Decrypt using AES-128-CTR
  mbedtls_aes_context aes_ctx;
  mbedtls_aes_init(&aes_ctx);

  size_t nc_off = 0;
  uint8_t nonce_counter[16];
  uint8_t stream_block[16];

  memcpy(nonce_counter, iv, 16);

  int ret = mbedtls_aes_setkey_enc(&aes_ctx, aes_key.data(), 128);
  if (ret != 0) {
    ESP_LOGE(TAG, "AES key setup failed: %d", ret);
    mbedtls_aes_free(&aes_ctx);
    return false;
  }

  ret = mbedtls_aes_crypt_ctr(&aes_ctx, plaintext_length, &nc_off,
                               nonce_counter, stream_block,
                               cipher_data, plaintext);

  mbedtls_aes_free(&aes_ctx);

  if (ret != 0) {
    ESP_LOGE(TAG, "AES decryption failed: %d", ret);
    return false;
  }

  ESP_LOGD(TAG, "Decryption successful, plaintext length: %u bytes", plaintext_length);
  return true;
}

}  // namespace multical21_wmbus
}  // namespace esphome
