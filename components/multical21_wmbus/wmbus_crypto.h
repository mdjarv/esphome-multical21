#pragma once

#include "wmbus_types.h"
#include <array>
#include <cstdint>

namespace esphome {
namespace multical21_wmbus {

/**
 * @brief Cryptography utilities for wMBUS packets
 *
 * Handles all cryptographic operations including CRC calculation
 * and AES-128-CTR decryption for wMBUS Mode C packets.
 *
 * Responsibility: Isolated crypto operations with no hardware dependencies.
 * Extracted from: multical21_wmbus.cpp lines 615-727
 */
class WMBusCrypto {
 public:
  /**
   * @brief Calculate CRC-16-EN-13757-4 checksum
   *
   * Implements the CRC algorithm specified in EN 13757-4 for wMBUS packets.
   *
   * @param data Pointer to data buffer
   * @param length Number of bytes to process
   * @return 16-bit CRC value
   */
  static uint16_t calculate_crc(const uint8_t *data, uint8_t length);

  /**
   * @brief Decrypt AES-128-CTR encrypted wMBUS payload
   *
   * Decrypts the encrypted portion of a wMBUS packet using AES-128 in CTR mode.
   * The IV is automatically constructed from the packet header per EN 13757-4.
   *
   * @param packet Pointer to complete packet buffer (including header)
   * @param packet_length Total length of packet (L-field value)
   * @param aes_key 16-byte AES-128 encryption key
   * @param plaintext Output buffer for decrypted data (must be at least 64 bytes)
   * @param plaintext_length Output parameter - receives length of decrypted data
   * @return true if decryption succeeded, false on error
   */
  bool decrypt_packet(const uint8_t *packet,
                      uint8_t packet_length,
                      const std::array<uint8_t, 16> &aes_key,
                      uint8_t *plaintext,
                      uint8_t &plaintext_length);

 private:
  /**
   * @brief Build AES-CTR initialization vector from packet header
   *
   * Constructs the 16-byte IV according to EN 13757-4 Section 7.2.
   *
   * @param packet Pointer to packet buffer (includes header)
   * @param iv Output buffer for 16-byte IV (must be pre-allocated)
   */
  void build_iv_(const uint8_t *packet, uint8_t *iv);
};

}  // namespace multical21_wmbus
}  // namespace esphome
