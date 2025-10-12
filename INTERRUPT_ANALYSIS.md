# Interrupt and Packet Reception Analysis

## Current Status
- **Register Configuration**: ✓ Verified identical to working reference implementation
- **Radio State**: MARC=0x0D (RX mode) - ✓ Correct
- **Interrupts**: User reports interrupts ARE increasing - ✓ ISR is firing
- **Problem**: Despite interrupts firing, no packets are being processed (RXbytes=0)

## Key Findings

### 1. Register Configuration Matches Reference
All CC1101 registers in ESPHome implementation match the working esp-multical21 reference:
- PKTCTRL0 = 0x02 (infinite packet length mode)
- PKTLEN = 0x30 (48 bytes max)
- IOCFG0 = 0x06 (GDO0: assert on sync word, deassert at end of packet)
- All other registers identical

### 2. Interrupt Is Firing
User confirms interrupt counter is increasing, which means:
- GDO0 pin is transitioning (HIGH -> LOW)
- ISR is being called correctly
- Interrupt attachment is working

### 3. But No Packets in FIFO
- RXbytes = 0 consistently
- No data is being read from FIFO
- No packets are being processed

## Possible Root Causes

### Theory 1: Interrupt Firing on Wrong Event
GDO0 with IOCFG0=0x06 should:
- Go HIGH when sync word detected
- Go LOW at end of packet

If interrupt is firing but RXbytes=0, possibilities:
- Interrupt firing on RISING edge instead of FALLING? (No - code shows FALLING)
- Interrupt firing too early (before data in FIFO)?
- GDO0 glitching and triggering spurious interrupts?

### Theory 2: Packet Data Not Making It to FIFO
Even though sync word is detected (causing GDO0 HIGH), the packet data might not be:
- Properly demodulated
- Written to FIFO
- Complete before interrupt

This could happen if:
- Timing issue with infinite packet length mode
- Carrier sense settings
- Sync word threshold

### Theory 3: FIFO Being Cleared Before Read
The start_rx() sequence includes:
```cpp
send_strobe_(CC1101_SFRX);  // Flush RX FIFO
```

If this is being called at the wrong time, it could clear data before reading.

### Theory 4: Data Is There But Not Being Read Correctly
- SPI communication issue with FIFO read
- Wrong address or read mode for RXFIFO
- ESPHome SPI abstraction issue

## Comparison with Working Reference

### Working Reference Flow (WaterMeter.cpp)
```cpp
loop():
  if (packetAvailable):  // ISR set this flag
    detachInterrupt()
    packetAvailable = false
    receive()           // Read FIFO immediately
    startReceiver()     // Flush and restart
    attachInterrupt()
```

### ESPHome Flow (multical21_wmbus.cpp)
```cpp
loop():
  if (packet_ready_):
    detachInterrupt()
    packet_ready_ = false
    packets_received_++
    read_fifo_into_packet_buffer_()  // Read FIFO
    radio_.start_rx()                // Restart receiver
    attachInterrupt()
```

**Key Difference**: ESPHome calls `radio_.start_rx()` which includes IDLE + FLUSH + RX.
The flush might be clearing data that should have been read!

## Next Steps to Debug

### Option 1: Add Detailed Logging in ISR Path
```cpp
void packet_isr_():
  instance->packet_ready_ = true
  // Log GDO0 state, RXBYTES register value HERE
```

### Option 2: Check RXBYTES Before Flushing
In `loop()` when interrupt fires:
```cpp
if (packet_ready_):
  uint8_t rxbytes_before = radio_.get_rx_bytes()
  // Log this value
  // If it's 0, interrupt is spurious
  // If it's >0, data IS in FIFO
```

### Option 3: Don't Flush on Error
Current code calls `start_rx()` (which flushes) even when read fails.
This might be clearing real data.

### Option 4: Check Actual GDO0 Pin Timing
Use oscilloscope or logic analyzer to see:
- When does GDO0 go HIGH (sync word detected)?
- When does it go LOW (triggering interrupt)?
- What's the timing between these events?
- Is there data in FIFO when GDO0 goes LOW?

## ROOT CAUSE IDENTIFIED: Reading FIFO After Entering IDLE

### Working Reference Code (WaterMeter.cpp line 627-704)
```cpp
void WaterMeter::receive() {
  uint8_t p1 = readByteFromFifo();  // Read preamble IMMEDIATELY
  uint8_t p2 = readByteFromFifo();
  payload[0] = readByteFromFifo();  // Read L-field
  // ... read rest of packet while radio is still in RX/RX_END state

  // ONLY AFTER reading everything:
  startReceiver();  // <- This enters IDLE, flushes, and restarts
}
```

**Key**: Reads FIFO immediately when interrupt fires, while radio is in RX or RX_END state.

### ESPHome Code (multical21_wmbus.cpp:182-205)
```cpp
bool read_fifo_into_packet_buffer_() {
  // Enter IDLE to safely read FIFO  ← WRONG!
  this->radio_.enter_idle();
  delay(2);

  // Then try to read FIFO...
  PacketBuffer pkt;
  uint8_t length;
  if (!this->read_packet_from_fifo_(pkt.data, length)) {
    return false;
  }
  //...
}
```

**Problem**: Enters IDLE first, which might:
1. Clear the FIFO automatically (depending on MCSM1 settings)
2. Reset the FIFO pointers
3. Cause the CC1101 to discard packet data

### The Fix

**Remove the `enter_idle()` call from `read_fifo_into_packet_buffer_()`**:
```cpp
bool read_fifo_into_packet_buffer_() {
  // Check if packet buffer has space
  if (this->packet_buffer_.is_full()) {
    ESP_LOGW(TAG, "Packet buffer full - dropping packet");
    return false;
  }

  // DON'T enter IDLE - read FIFO directly!
  // The packet is complete (GDO0 went LOW), data is in FIFO

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
```

Then `loop()` calls `start_rx()` which handles IDLE + flush + restart AFTER reading.
