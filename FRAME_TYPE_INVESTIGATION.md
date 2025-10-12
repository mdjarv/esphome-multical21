# Multical21 Frame Type Investigation

**Date Started**: 2025-10-11
**Status**: Testing In Progress - First Packet Received Successfully
**Goal**: Understand compact vs long frame behavior and verify correct handling

---

## Background: The Original Question

> "It's hard to predict send interval on the multical21, and it almost seems like it varies. Could it be adaptive, sending more often when water measurements change, and more seldom when no change is detected?"

This led us to investigate whether:
1. The meter uses adaptive transmission intervals
2. Different frame types (compact vs long) affect timing
3. We're correctly handling both frame types

---

## What We Discovered

### Frame Types in wMBUS Protocol

The Multical21 sends two types of frames:

#### **Compact Frames** (most common - ~87.5% of packets)
- **Marker**: Byte 2 of decrypted plaintext ≠ 0x78
- **Structure**: Minimal headers, omits DIF/VIF metadata
- **Purpose**: Save battery by transmitting less data
- **Frequency**: Expected 7 out of every 8 transmissions

#### **Long Frames** (periodic - ~12.5% of packets)
- **Marker**: Byte 2 of decrypted plaintext = 0x78
- **Structure**: Full DIF/VIF headers included
- **Purpose**: Self-documenting, standards-compliant
- **Frequency**: Expected 1 out of every 8 transmissions (~every 2 minutes at 16s intervals)

### Key Finding: Same Data, Different Structure

**Both frame types contain identical meter data:**
- Total water consumption (4 bytes, 1 liter precision)
- Target water consumption (4 bytes, 1 liter precision)
- Flow temperature (1 byte, 1°C precision)
- Ambient temperature (1 byte, 1°C precision)
- Status/info codes (1 byte)

**The ONLY difference**: Field positions within the decrypted plaintext

| Field | Compact Frame Position | Long Frame Position |
|-------|------------------------|---------------------|
| Info Codes | byte 7 | byte 6 |
| Total Water | bytes 9-12 | bytes 10-13 |
| Target Water | bytes 13-16 | bytes 16-19 |
| Flow Temp | byte 17 | byte 22 |
| Ambient Temp | byte 18 | byte 25 |

---

## What We Implemented

### 1. Enhanced Data Structures

**`WMBusMeterData` (wmbus_packet_parser.h:16-40)**
- Added `frame_type` (string: "compact" or "long")
- Added `plaintext_length` (uint8_t: decrypted data length)
- Added `frame_marker` (uint8_t: byte 2 value for analysis)

**`MeterStats` (wmbus_types.h:120-130)**
- Added `compact_frame_count` (uint32_t)
- Added `long_frame_count` (uint32_t)
- Added `last_frame_type` (string)

### 2. Enhanced Logging

**Parser (wmbus_packet_parser.cpp:81-92)**
```cpp
ESP_LOGI(TAG, ">>> Frame Type: %s (marker=0x%02X, length=%u bytes) <<<",
         data.frame_type.c_str(), data.frame_marker, length);
```
- Logs frame type prominently at INFO level
- Shows marker byte (0x78 for long, other for compact)
- Shows plaintext length
- Includes hex dump of first 30 bytes (DEBUG level)

**Statistics Tracking (multical21_wmbus.cpp:85-87)**
```cpp
ESP_LOGI(TAG, "Interval: %u.%u sec (avg: %u sec, count: %u, frame: %s)",
         interval_ms / 1000, (interval_ms % 1000) / 100,
         avg_interval_sec, stats.packet_count + 1, frame_type.c_str());
```
- Each packet logs its frame type with timing info

**Periodic Summary (multical21_wmbus.cpp:330-335)**
```cpp
ESP_LOGI(TAG, "  Frame types: compact=%u, long=%u, last=%s",
         stats.compact_frame_count, stats.long_frame_count, stats.last_frame_type.c_str());
float compact_ratio = (float)stats.compact_frame_count / (total) * 100.0f;
ESP_LOGI(TAG, "  Compact ratio: %.1f%% (expect ~87.5%% = 7/8)", compact_ratio);
```
- Shows cumulative frame type counts
- Calculates compact ratio (should be ~87.5% = 7/8)

### 3. Files Modified

1. **wmbus_packet_parser.h** - Added frame analysis fields to WMBusMeterData
2. **wmbus_packet_parser.cpp** - Enhanced logging and frame detection
3. **wmbus_types.h** - Added frame tracking to MeterStats
4. **multical21_wmbus.h** - Updated update_meter_stats_ signature
5. **multical21_wmbus.cpp** - Implemented frame tracking and statistics display

---

## Expected Test Results

### Hypothesis: 7:1 Pattern

If the Multical21 follows standard wMBUS behavior:
- **Pattern**: 7 compact frames → 1 long frame → repeat
- **Compact ratio**: ~87.5% (7/8 of packets)
- **Long ratio**: ~12.5% (1/8 of packets)
- **Timing**: Same ~16 second interval for both types

### What to Look For

#### 1. Frame Type Distribution
```
Expected after 24 packets:
  Compact: ~21 packets
  Long: ~3 packets
  Ratio: ~87.5% compact
```

#### 2. Frame Type Pattern
Look for regular alternation:
```
Packet #1:  compact
Packet #2:  compact
Packet #3:  compact
Packet #4:  compact
Packet #5:  compact
Packet #6:  compact
Packet #7:  compact
Packet #8:  LONG     ← every 8th packet
Packet #9:  compact
...repeat...
```

#### 3. Plaintext Lengths
- **Compact frames**: Likely 19-25 bytes
- **Long frames**: Likely 26-35 bytes
- Long frames should be noticeably longer

#### 4. Byte 2 Marker
- **Compact frames**: Various values (likely 0x79 or others)
- **Long frames**: Always 0x78

#### 5. Timing Correlation
- Both types should arrive at ~16 second intervals
- No significant timing difference between frame types
- If intervals seem variable, it's likely due to:
  - Missed packets (RF conditions)
  - Slight meter timing variations
  - NOT due to frame type

#### 6. Data Consistency
- Total consumption should be identical in consecutive packets (or increase)
- Temperatures should vary gradually
- Status codes should remain constant (usually 0x00)

---

## Next Steps: Testing Protocol

### Step 1: Compile and Upload
```bash
cd /c/Projects/esphome-multical21
python -m esphome compile example.yaml
python -m esphome upload example.yaml
```

### Step 2: Monitor Logs
```bash
python -m esphome logs example.yaml --device COM13
```

### Step 3: Collect Data (20-30 minutes)

Watch for these log lines:
```
[I][multical21_wmbus.parser:81] >>> Frame Type: compact (marker=0x??, length=?? bytes) <<<
[I][multical21_wmbus.parser:81] >>> Frame Type: long (marker=0x78, length=?? bytes) <<<
[I][multical21_wmbus:85] Interval: 16.2 sec (avg: 16 sec, count: 10, frame: compact)
```

Every ~60 seconds, you'll see the summary:
```
[I][multical21_wmbus:330] Frame types: compact=7, long=1, last=compact
[I][multical21_wmbus:334] Compact ratio: 87.5% (expect ~87.5% = 7/8)
```

### Step 4: Record Observations

Create a table like this:

| Packet # | Timestamp | Frame Type | Marker | Length | Interval | Water m³ | Temps |
|----------|-----------|------------|--------|--------|----------|----------|-------|
| 1 | 00:00:00 | compact | 0x79 | 21 | - | 123.456 | 18°C/22°C |
| 2 | 00:00:16 | compact | 0x79 | 21 | 16s | 123.456 | 18°C/22°C |
| ... | ... | ... | ... | ... | ... | ... | ... |
| 8 | 00:01:52 | LONG | 0x78 | 28 | 16s | 123.456 | 18°C/22°C |
| 9 | 00:02:08 | compact | 0x79 | 21 | 16s | 123.456 | 18°C/22°C |

### Step 5: Analysis Questions

After collecting 20+ packets, answer:

1. **Does the 7:1 pattern hold?**
   - Count compact vs long frames
   - Is every 8th packet a long frame?

2. **Are plaintext lengths consistent?**
   - Do compact frames cluster around one length?
   - Are long frames always longer?

3. **Is the marker byte reliable?**
   - Are all long frames exactly 0x78?
   - What values appear for compact frames?

4. **Do both types decode correctly?**
   - Do all packets parse successfully?
   - Are field positions working for both types?

5. **Is there a timing correlation?**
   - Do long frames take longer to arrive?
   - Is the interval truly ~16 seconds for both?
   - What's the actual variation in intervals?

6. **Do values match between frame types?**
   - If you have a compact followed by a long (or vice versa):
   - Does total consumption match exactly?
   - Do temperatures match?

---

## Questions to Answer

### Primary Question
**"Does the Multical21 use adaptive transmission intervals based on water usage?"**

**Hypothesis**: No - the meter transmits at a fixed ~16 second interval regardless of usage.

**Evidence needed**:
- Consistent intervals even when water is flowing vs. idle
- No correlation between consumption changes and interval variation

### Secondary Questions

1. **Are we handling both frame types correctly?**
   - Do both parse without errors?
   - Do both produce valid meter readings?

2. **What causes the perceived interval variations?**
   - Missed packets due to RF conditions?
   - Meter timing jitter?
   - Processing time variations?

3. **What's the actual frame type pattern?**
   - Is it exactly 7:1?
   - Is it consistent?
   - Does it ever deviate?

---

## Troubleshooting

### If No Long Frames Appear

**Possible causes:**
1. Not waiting long enough (need 2+ minutes)
2. Multical21 doesn't use long frames (some meters don't)
3. Frame detection logic is wrong

**Action**: Collect 50+ packets (15+ minutes) to be sure

### If All Packets are "unknown" Frame Type

**Check**: Look at the DEBUG logs for "Plaintext hex:"
- Find byte 2 in the hex dump
- Is it 0x78 for some packets?
- If not, our detection logic may need adjustment

### If Parsing Fails for One Frame Type

**Symptoms**: Some packets fail with "Failed to parse meter data"

**Check**:
1. Plaintext length - is it sufficient for field positions?
2. Marker byte - does frame type detection work?
3. Field positions - are they accessing valid bytes?

### If Intervals ARE Correlated with Frame Type

**This would be unexpected** but interesting!

If long frames consistently take longer:
- Note the difference (seconds)
- Check if over-air time explains it
- Could indicate processing bottleneck

---

## Success Criteria

✅ **Investigation Complete** when we can answer:

1. What percentage of packets are compact vs long?
2. What's the pattern (if any)?
3. Are both frame types handled correctly?
4. Do intervals correlate with frame type or water usage?
5. What actually causes interval variations?

---

## Code Reference

### Key Functions

**Frame Detection**: `wmbus_packet_parser.cpp:14-16`
```cpp
bool is_long_frame_(const uint8_t *plaintext) {
  return (plaintext[2] == 0x78);
}
```

**Frame Parsing**: `wmbus_packet_parser.cpp:44-93`
- Lines 54-60: Store frame analysis data
- Lines 65-79: Set field positions based on frame type
- Lines 81-92: Log frame info and hex dump

**Statistics Tracking**: `multical21_wmbus.cpp:76-114`
- Lines 92-98: Count frame types
- Lines 109-111: Initialize frame counts for new meter

**Statistics Display**: `multical21_wmbus.cpp:329-335`
- Shows cumulative frame counts
- Calculates compact ratio
- Compares to expected 87.5%

---

## Related Documentation

- **WMBUS_IMPLEMENTATION_SPEC.md** - Section 8.1 (Frame Type Detection)
- **wmbusmeters GitHub** - Reference implementation
- **EN 13757-4** - Official wMBUS protocol standard

---

## Notes for Future Development

### If We Confirm 7:1 Pattern

Could add:
- Prediction of next long frame
- Warning if pattern breaks (may indicate meter issue)
- Optimization: different handling for each type?

### If Pattern Differs

Document actual pattern:
- Is it regular?
- Does it depend on meter configuration?
- Regional/firmware differences?

### If Intervals ARE Adaptive

This would be a significant finding:
- Document trigger conditions
- Analyze battery impact
- Could optimize receiver expectations

---

## Status: Ready for Testing

All code changes are complete and ready to compile/test.

**Next action**:
```bash
python -m esphome compile example.yaml
python -m esphome upload example.yaml
python -m esphome logs example.yaml --device COM13
```

Then collect and analyze 20-30 minutes of logs.

---

## Testing Session: 2025-10-11 (Evening)

### Compilation Fix

**Issue**: Missing `#include <string>` header in `wmbus_types.h:4`

**Error**:
```
error: 'string' in namespace 'std' does not name a type
  129 |   std::string last_frame_type;  // "compact" or "long"
```

**Fix Applied**: Added `#include <string>` to wmbus_types.h between `<cstdint>` and `<vector>`

**Result**: ✅ Compilation successful, upload successful

### First Packet Received: 23:27:32

**Packet Details**:
```
Frame Type: compact (marker=0x79, length=19 bytes)
Plaintext hex: FA 79 79 ED A8 EF AE 00 00 DD ED 16 00 EA DA 16 00 11 15
Status: normal (0x00)
Total consumption: 1502.685 m³
Target consumption: 1497.834 m³
Flow temperature: 17 °C
Ambient temperature: 21 °C
```

**Key Findings**:
- ✅ Frame type detection **WORKING** - Correctly identified compact frame
- ✅ Marker byte detection **WORKING** - Found 0x79 (not 0x78, so compact)
- ✅ Plaintext length **AS EXPECTED** - 19 bytes for compact frame
- ✅ Decryption **SUCCESSFUL** - AES-128 working correctly
- ✅ Field parsing **SUCCESSFUL** - All values extracted correctly
- ✅ Sensor publishing **WORKING** - Data sent to Home Assistant

**Frame Type Analysis**:
- Marker byte: 0x79 (compact frame indicator)
- Length: 19 bytes (within expected compact range of 19-25 bytes)
- Field positions used: Compact frame offsets (bytes 7, 9-12, 13-16, 17, 18)

### Environmental Challenges

**Discovery**: Packet reception is intermittent due to:
- Distance between ESP32 and meter
- Concrete walls and other meters in between
- Development environment (not final installation location)

**Impact**:
- Took ~10 minutes to receive first packet after boot
- High interrupt count but low packet success rate
- Expected intervals will appear longer due to missed packets
- Will need longer monitoring session to collect sufficient data

**Implication**:
- Frame type detection code is confirmed working
- Need 30-60+ minutes of monitoring to collect 20+ packets
- Packet loss will make interval measurements unreliable
- But frame type distribution (compact vs long ratio) should still be accurate

### Next Steps

**Immediate**:
1. Continue monitoring for extended period (30-60 minutes)
2. Collect at least 20 valid packets
3. Watch for first long frame (marker=0x78)
4. Observe periodic statistics summary (every ~60 seconds)

**Data to Collect**:
- Frame type for each packet (compact vs long)
- Marker byte values
- Plaintext lengths
- Cumulative frame counts
- Compact ratio percentage

**Success Criteria** (adjusted for RF conditions):
- Observe both compact and long frames
- Verify marker byte: 0x78 for long, other values for compact
- Confirm compact ratio trends toward ~87.5% (7/8) over time
- Validate all packets parse successfully regardless of frame type

### Code Verification Status

**What We've Confirmed**:
- ✅ Compilation successful (after string header fix)
- ✅ Upload successful to ESP32-C3
- ✅ Radio initialization successful (CC1101 in RX mode)
- ✅ Packet reception working (with RF limitations)
- ✅ CRC verification working
- ✅ AES decryption working
- ✅ Frame type detection logic working
- ✅ Compact frame parsing working
- ✅ Sensor publishing working
- ⏳ Long frame parsing - awaiting first long frame to verify
- ⏳ Statistics tracking - awaiting more packets
- ⏳ Periodic summary - awaiting 60-second interval

**Still to Verify**:
1. Long frame detection (marker=0x78)
2. Long frame field positions (bytes 6, 10-13, 16-19, 22, 25)
3. Frame type statistics accumulation
4. Compact ratio calculation
5. Pattern observation (7:1 compact:long)

---

**Investigation led by**: Claude Code
**User**: mathi (Karlskrona, Sweden)
**Hardware**: ESP32-C3 + CC1101 + Multical21 water meter
**Location**: Development setup with RF obstacles (concrete, meters)
