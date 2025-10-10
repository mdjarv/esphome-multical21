#pragma once

#include "wmbus_types.h"
#include "esphome/core/log.h"

namespace esphome {
namespace multical21_wmbus {

/**
 * @brief Thread-safe ring buffer for ISR-to-loop packet passing
 *
 * This template class provides a fixed-size ring buffer optimized for
 * interrupt service routine (ISR) to loop() communication. It uses volatile
 * members to ensure correct operation across ISR/main code boundaries.
 *
 * @tparam SIZE Number of packet slots in the ring buffer (default: 4)
 *
 * Thread Safety:
 * - push() is ISR-safe and can be called from interrupt context
 * - pop() should only be called from loop() context
 * - No mutual exclusion needed due to single-producer, single-consumer design
 *
 * Usage Example:
 * @code
 * WMBusPacketBuffer<4> buffer;  // Create buffer with 4 slots
 *
 * // In ISR:
 * PacketBuffer pkt;
 * // ... fill pkt.data, pkt.length, pkt.timestamp ...
 * pkt.valid = true;
 * if (!buffer.push(pkt)) {
 *   // Buffer full - packet dropped
 * }
 *
 * // In loop():
 * PacketBuffer pkt;
 * while (buffer.pop(pkt)) {
 *   // Process pkt
 * }
 * @endcode
 */
template<size_t SIZE = PACKET_RING_SIZE>
class WMBusPacketBuffer {
 public:
  /**
   * @brief Construct a new packet buffer
   */
  WMBusPacketBuffer() : read_idx_(0), write_idx_(0) {
    // Initialize all packets as invalid
    for (size_t i = 0; i < SIZE; i++) {
      ring_[i].valid = false;
      ring_[i].length = 0;
      ring_[i].timestamp = 0;
    }
  }

  /**
   * @brief Add a packet to the ring buffer (ISR-safe)
   *
   * @param packet The packet to add
   * @return true if packet was added successfully
   * @return false if buffer is full (packet dropped)
   */
  bool push(const PacketBuffer &packet) {
    uint8_t write_idx = write_idx_;
    uint8_t next_write = (write_idx + 1) % SIZE;

    // Check if buffer is full
    if (next_write == read_idx_) {
      return false;  // Buffer full - packet dropped
    }

    // Copy packet data
    volatile PacketBuffer *buf = &ring_[write_idx];
    memcpy((void*)buf->data, packet.data, packet.length);
    buf->length = packet.length;
    buf->timestamp = packet.timestamp;
    buf->valid = packet.valid;

    // Advance write pointer (atomic operation on single byte)
    write_idx_ = next_write;

    return true;
  }

  /**
   * @brief Remove and return a packet from the ring buffer
   *
   * @param packet Output parameter to receive the packet
   * @return true if a packet was retrieved
   * @return false if buffer is empty
   */
  bool pop(PacketBuffer &packet) {
    // Check if buffer is empty
    if (read_idx_ == write_idx_) {
      return false;
    }

    uint8_t read_idx = read_idx_;
    volatile PacketBuffer *pbuf = &ring_[read_idx];

    // Skip invalid packets
    if (!pbuf->valid) {
      read_idx_ = (read_idx + 1) % SIZE;
      return false;
    }

    // Copy to non-volatile buffer
    memcpy(packet.data, (const uint8_t*)pbuf->data, pbuf->length);
    packet.length = pbuf->length;
    packet.timestamp = pbuf->timestamp;
    packet.valid = pbuf->valid;

    // Mark as consumed
    pbuf->valid = false;

    // Advance read pointer
    read_idx_ = (read_idx + 1) % SIZE;

    return true;
  }

  /**
   * @brief Check if the buffer is empty
   *
   * @return true if buffer is empty
   * @return false if buffer contains packets
   */
  bool is_empty() const {
    return read_idx_ == write_idx_;
  }

  /**
   * @brief Check if the buffer is full
   *
   * @return true if buffer is full
   * @return false if buffer has space
   */
  bool is_full() const {
    uint8_t next_write = (write_idx_ + 1) % SIZE;
    return next_write == read_idx_;
  }

  /**
   * @brief Clear all packets from the buffer
   */
  void clear() {
    for (size_t i = 0; i < SIZE; i++) {
      ring_[i].valid = false;
    }
    read_idx_ = 0;
    write_idx_ = 0;
  }

  /**
   * @brief Get the number of packets currently in the buffer
   *
   * @return size_t Number of packets
   */
  size_t size() const {
    if (write_idx_ >= read_idx_) {
      return write_idx_ - read_idx_;
    } else {
      return SIZE - read_idx_ + write_idx_;
    }
  }

  /**
   * @brief Get the capacity of the buffer
   *
   * @return size_t Maximum number of packets the buffer can hold
   */
  constexpr size_t capacity() const {
    return SIZE;
  }

 private:
  volatile PacketBuffer ring_[SIZE];  // Ring buffer storage
  volatile uint8_t read_idx_;         // Read index (consumer)
  volatile uint8_t write_idx_;        // Write index (producer)
};

}  // namespace multical21_wmbus
}  // namespace esphome
