# Multical21 wMBUS Receiver - ESPHome Component

An ESPHome external component for reading Kamstrup Multical21 water meters via wMBUS (Wireless M-Bus) protocol using an ESP32-C3 and CC1101 sub-GHz radio module.

## Features

- **Real-time water consumption monitoring** (total and target values in m³)
- **Temperature sensors** (flow and ambient temperatures)
- **Status monitoring** (meter info codes)
- **Automatic packet decryption** using AES-128-CTR
- **CRC validation** per EN 13757-4 standard
- **Radio health monitoring** with automatic recovery
- **Home Assistant integration** via ESPHome API

## Hardware Requirements

### Components

1. **ESP32-C3 Super Mini** - Microcontroller board
2. **CC1101 Sub-GHz Radio Module** - 868 MHz version for European wMBUS
3. **Kamstrup Multical21 Water Meter** - with wMBUS transmitter

### Wiring

Connect the CC1101 to ESP32-C3 as follows:

| ESP32-C3 Pin | CC1101 Pin | Function |
|--------------|------------|----------|
| 3.3V | VCC | Power (⚠️ **Must be 3.3V**) |
| GND | GND | Ground |
| GPIO7 | CSN (CS) | SPI Chip Select |
| GPIO6 | MOSI (SI) | SPI Data Out |
| GPIO5 | MISO (SO) | SPI Data In |
| GPIO4 | SCK (SCLK) | SPI Clock |
| GPIO3 | GDO0 | Interrupt (packet ready) |

**⚠️ Important:** The CC1101 is **NOT** 5V tolerant - use only 3.3V power supply!

### Wiring Diagram

```
┌─────────────────────┐              ┌──────────────────┐
│   ESP32-C3 Super    │              │     CC1101       │
│       Mini          │              │   Radio Module   │
├─────────────────────┤              ├──────────────────┤
│  3.3V      ●────────┼──────────────┼────●  VCC        │
│  GND       ●────────┼──────────────┼────●  GND        │
│  GPIO 7    ●────────┼──────────────┼────●  CSN (CS)   │
│  GPIO 6    ●────────┼──────────────┼────●  MOSI (SI)  │
│  GPIO 5    ●────────┼──────────────┼────●  MISO (SO)  │
│  GPIO 4    ●────────┼──────────────┼────●  SCK (SCLK) │
│  GPIO 3    ●────────┼──────────────┼────●  GDO0       │
└─────────────────────┘              └──────────────────┘
```

## Software Requirements

- **ESPHome** 2024.2.0 or later
- **ESP32 platform** with mbedTLS support (included automatically)
- **SPI component** (declared as dependency, loaded automatically)
- **Home Assistant** (optional, for integration)

## Installation

This component can be added to your ESPHome configuration in two ways:

1. **As an external component from GitHub** (recommended) - No cloning required
2. **Local development** - Clone and modify the component

### Method 1: External Component from GitHub (Recommended)

The easiest way to use this component is to reference it directly from GitHub in your ESPHome configuration. ESPHome will automatically download and use it.

Add the following to your ESPHome YAML configuration:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/mdjarv/esphome-multical21
      ref: main  # or specify a version tag like v1.0.0
    components: [ multical21_wmbus ]
```

**Complete Example Configuration:**

```yaml
esphome:
  name: water-meter
  friendly_name: Water Meter

esp32:
  board: esp32-c3-devkitm-1
  framework:
    type: arduino

# Reference the external component from GitHub
external_components:
  - source:
      type: git
      url: https://github.com/mdjarv/esphome-multical21
      ref: master
    components: [ multical21_wmbus ]

# Required: SPI bus configuration
spi:
  clk_pin: GPIO4
  mosi_pin: GPIO6
  miso_pin: GPIO5

# Configure the water meter sensor
sensor:
  - platform: multical21_wmbus
    id: water_meter_component
    cs_pin: GPIO7         # SPI chip select
    gdo0_pin: GPIO3       # Interrupt pin

    # SECURITY: Use secrets.yaml!
    meter_id: !secret meter_id
    aes_key: !secret aes_key

    total_consumption:
      name: "Water Total"
    target_consumption:
      name: "Water Target"
    flow_temperature:
      name: "Flow Temperature"
    ambient_temperature:
      name: "Ambient Temperature"

text_sensor:
  - platform: multical21_wmbus
    multical21_wmbus_id: water_meter_component
    info_codes:
      name: "Meter Status"

# Standard ESPHome configuration
logger:
  level: DEBUG

api:
  encryption:
    key: !secret api_encryption_key

ota:
  - platform: esphome
    password: !secret ota_password

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
  ap:
    ssid: "Water Meter Fallback"
    password: "fallback12345"
```

### Method 2: Local Development

If you want to modify the component or use it offline, clone the repository:

```bash
git clone https://github.com/mdjarv/esphome-multical21.git
```

Then reference it locally in your config:

```yaml
external_components:
  - source:
      type: local
      path: /path/to/esphome-multical21/components
    components: [ multical21_wmbus ]
```

Or if you're working in the cloned repository directory:

```yaml
external_components:
  - source:
      type: local
      path: components
```

### Obtain Required Information

Before configuring, you need two pieces of information from your water meter:

#### Meter Serial Number
Look at your Multical21 meter - the serial number is printed on the device label. It's 8 hexadecimal digits.

**Example:** `3A9F7C2E` (yours will be different)

#### AES Encryption Key
Contact your water utility company and request the AES encryption key for your meter. You'll need to provide your meter serial number.

The key is 16 bytes (32 hexadecimal characters).

**Example format:** `B8F4E2D1C6A59B3E7F8D2A4C6E9B1F5A` (yours will be different)

### Configure Secrets

Create a `secrets.yaml` file next to your ESPHome configuration:

```yaml
# WiFi credentials
wifi_ssid: "YourWiFiSSID"
wifi_password: "YourWiFiPassword"

# API and OTA security
api_encryption_key: "your-32-character-base64-key-here="
ota_password: "your-ota-password"

# Meter configuration
meter_id: "3A9F7C2E"                                      # Your meter's serial number
aes_key: "B8F4E2D1C6A59B3E7F8D2A4C6E9B1F5A"            # Your meter's AES key
```

**🔒 Security Note:**
- Always use `secrets.yaml` for sensitive data - never hardcode secrets in your config
- The AES key is **CRITICAL** - it decrypts all your meter data
- Add `secrets.yaml` to `.gitignore` to prevent accidental commits
- Generate API encryption key: `esphome wizard` or any base64 generator

### Flash the Device

Using ESPHome command line:

```bash
esphome run your-config.yaml
```

Or use the ESPHome Dashboard:

1. Open ESPHome Dashboard
2. Add your configuration file
3. Connect ESP32-C3 via USB
4. Click "Install" → "Plug into this computer"
5. Monitor logs to verify operation

## Configuration Reference

### Required Configuration Sections

This component requires three main sections in your ESPHome configuration:

#### 1. External Component Declaration

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/mdjarv/esphome-multical21
      ref: main
    components: [ multical21_wmbus ]
```

#### 2. SPI Bus Configuration

```yaml
spi:
  clk_pin: GPIO4
  mosi_pin: GPIO6
  miso_pin: GPIO5
```

#### 3. Component Configuration

```yaml
sensor:
  - platform: multical21_wmbus
    id: water_meter_component
    cs_pin: GPIO7         # SPI chip select
    gdo0_pin: GPIO3       # Interrupt pin

    # SECURITY: Use secrets.yaml for sensitive data!
    meter_id: !secret meter_id    # Your meter serial number (8 hex digits)
    aes_key: !secret aes_key      # Your AES encryption key (32 hex chars)

    update_interval: 60s  # Optional, default is 60s

    # Optional sensors (comment out any you don't need)
    total_consumption:
      name: "Water Total"
    target_consumption:
      name: "Water Target"
    flow_temperature:
      name: "Flow Temperature"
    ambient_temperature:
      name: "Ambient Temperature"

text_sensor:
  - platform: multical21_wmbus
    multical21_wmbus_id: water_meter_component  # Must match sensor id above
    info_codes:
      name: "Meter Status"
```

**Required `secrets.yaml`:**
```yaml
meter_id: "3A9F7C2E"                                      # 8 hex digits
aes_key: "B8F4E2D1C6A59B3E7F8D2A4C6E9B1F5A"            # 32 hex characters
```

### Configuration Options

#### External Component Source Options

**From GitHub (recommended):**
```yaml
external_components:
  - source:
      type: git
      url: https://github.com/mdjarv/esphome-multical21
      ref: main  # Use 'main' for latest, or 'v1.0.0' for specific version
    components: [ multical21_wmbus ]
    refresh: 1d  # Optional: how often to check for updates (default: never)
```

**From local path:**
```yaml
external_components:
  - source:
      type: local
      path: /path/to/esphome-multical21/components
    components: [ multical21_wmbus ]
```

**Version Pinning:**
- Use `ref: main` for the latest version (may include breaking changes)
- Use `ref: v1.0.0` for a specific stable version (recommended for production)
- Use `refresh: 1d` to auto-update daily, or omit to cache permanently

**Clearing the cache:**
If you need to force ESPHome to re-download the component:
```bash
rm -rf ~/.esphome/.external_components/
```

### Available Sensors

| Sensor | Unit | Data Type | Description |
|--------|------|-----------|-------------|
| `total_consumption` | m³ | Float (3 decimals) | Cumulative water consumption since meter installation |
| `target_consumption` | m³ | Float (3 decimals) | Target/reference consumption value |
| `flow_temperature` | °C | Integer | Temperature of water flowing through meter |
| `ambient_temperature` | °C | Integer | Temperature around meter housing |
| `info_codes` | text | String | Meter status/error codes (see below) |

#### Info Codes (Status Values)

The `info_codes` text sensor reports the meter's operational status:

| Value | Description | Meaning |
|-------|-------------|---------|
| `normal` | Normal operation | Meter functioning correctly |
| `dry` | No water flow | No water detected for extended period |
| `reverse` | Reverse flow | Water flowing backwards through meter |
| `leak` | Leak detected | Continuous low flow suggesting a leak |
| `burst` | Burst detected | Sudden high flow suggesting pipe burst |
| `code_0xXX` | Unknown code | Unrecognized status code (XX = hex value) |

**Note**: Most of the time you'll see `normal`. Other values indicate potential issues that may require attention.

## Usage

### Home Assistant Integration

Once flashed and connected to WiFi, the device will automatically appear in Home Assistant (if you have the ESPHome integration installed).

You'll see the following entities:

- **sensor.water_total** - Total water consumption
- **sensor.water_target** - Target consumption value
- **sensor.flow_temperature** - Water temperature
- **sensor.ambient_temperature** - Meter ambient temperature
- **text_sensor.meter_status** - Meter status/info codes

### Monitoring

Check the ESPHome logs to verify operation:

```bash
esphome logs example.yaml
```

Expected log output:
```
[I][multical21_wmbus:xxx] CC1101 in RX mode
[I][multical21_wmbus.parser:xxx] >>> Frame Type: compact (marker=0x79, length=19 bytes) <<<
[I][multical21_wmbus:xxx] Status: normal (0x00)
[D][sensor:xxx] 'Water Total': Sending state 123.456 m³
```

## Troubleshooting

### Verifying Setup

Check the logs to verify component initialization:

```bash
esphome logs example.yaml
```

Look for successful initialization messages showing the radio is in RX mode and the meter ID is configured correctly.

### No Packets Received

1. **Check wiring** - Verify all connections, especially GND and 3.3V
2. **Verify SPI communication** - Look for "CC1101 in RX mode" in logs
3. **Check meter distance** - Move ESP32 closer to meter (< 10 meters for testing)
4. **Verify meter is transmitting** - Meter typically transmits every 8-16 seconds

### Meter ID Mismatch

- Verify your `meter_id` matches the serial number on your meter
- Check byte order - should be entered as printed on meter
- If you see other meter IDs in logs, neighbors may have similar meters nearby

### CRC Errors

- **Weak signal** or **interference** - Try repositioning the antenna or moving closer to meter
- Check for physical obstacles between receiver and meter

### Decryption Fails

- **Wrong AES key** - Double-check with your utility company
- **Wrong meter** - Verify meter ID is correct
- Check logs for specific error messages

### Radio Stops Receiving

- Component includes automatic health monitoring and recovery
- Verify GDO0 interrupt pin connection

## Expected Performance

- **Packet interval:** 8-16 seconds (meter dependent)
- **Success rate:** > 95% in good conditions (< 10m range, no obstacles)
- **Power consumption:** ~150mA active (ESP32-C3 + CC1101)

## Security Best Practices

**IMPORTANT:** Protect your AES encryption key - it's the critical secret for decrypting meter data.

### Key Security Measures

1. **Use `secrets.yaml`** - Always store sensitive data (`aes_key`, WiFi credentials, API keys) in `secrets.yaml`, never in configuration files
2. **Never commit secrets** - The `secrets.yaml` file is in `.gitignore` to prevent accidental commits
3. **Enable API encryption** - Use Home Assistant API encryption (configured in example)
4. **Use OTA passwords** - Protect firmware updates with a strong password
5. **Physical security** - Secure the device as the AES key is stored in flash memory

### Secret Sensitivity Levels

| Secret | Risk Level | Purpose |
|--------|------------|---------|
| `aes_key` | **CRITICAL** | Decrypts all meter data |
| `wifi_password` | High | Network access |
| API encryption key | High | Device control |
| OTA password | High | Firmware updates |
| `meter_id` | Low | Meter ID (printed on device) |

If you believe your AES key has been compromised, contact your water utility immediately to request a new key.

## Technical Details

### Protocol

- **Frequency:** 868.95 MHz (European wMBUS band)
- **Modulation:** 2-FSK
- **Data rate:** 100 kbps
- **Mode:** wMBUS Mode C1 (unidirectional meter → collector)
- **Encryption:** AES-128-CTR (using mbedTLS)
- **Standard:** EN 13757-4
- **Max packet size:** 64 bytes
- **Sync word:** 0x543D (wMBUS Mode C)

### Communication

- **SPI speed:** 4 MHz
- **SPI mode:** Mode 0 (CPOL=0, CPHA=0)
- **Bit order:** MSB first
- **Polling interval:** 60 seconds (configurable)
- **Reception timeout:** 5 minutes (triggers automatic radio restart)
- **Health check:** Every 10 seconds

### Component Architecture

The component implements:

1. **CC1101 SPI driver** - Low-level radio control
2. **wMBUS packet decoder** - Preamble, length, payload parsing
3. **CRC validation** - EN 13757-4 CRC-16 algorithm
4. **AES-128-CTR decryption** - Using mbedTLS
5. **Meter data parser** - Supports compact and long frame formats
6. **Health monitoring** - Automatic radio recovery

## Development

### Project Structure

```
esphome-multical21/
├── components/
│   └── multical21_wmbus/
│       ├── __init__.py                # Python package marker
│       ├── sensor.py                  # Sensor config validation
│       ├── text_sensor.py             # Text sensor config validation
│       ├── multical21_wmbus.h         # Main component header
│       ├── multical21_wmbus.cpp       # Main component implementation
│       ├── cc1101_radio.h/cpp         # CC1101 radio driver
│       ├── wmbus_crypto.h/cpp         # AES decryption
│       ├── wmbus_packet_parser.h/cpp  # Packet parsing logic
│       ├── wmbus_packet_buffer.h      # Packet buffering
│       └── wmbus_types.h              # Type definitions
├── example.yaml                        # Example configuration
├── secrets.yaml.example                # Template for secrets
├── WMBUS_IMPLEMENTATION_SPEC.md       # Protocol specification
└── README.md                           # This file
```

### Building from Source

1. Clone this repository
2. Place in ESPHome's `external_components` directory or use local path
3. Reference in your YAML configuration

### Testing

To enable detailed logging for troubleshooting:

```yaml
logger:
  level: DEBUG  # Use VERBOSE for even more detail
```

Success indicators in logs:
- "CC1101 in RX mode" - Radio configured and receiving
- "Frame Type: compact/long" - Packets being received
- "'Water Total': Sending state" - Data publishing to sensors

## Credits

Based on the Multical21 wMBUS implementation specification documenting the complete protocol, radio configuration, and packet structure.

## License

[Your chosen license here]

## Support

For issues, questions, or contributions:

- Open an issue on GitHub
- Check the [WMBUS_IMPLEMENTATION_SPEC.md](WMBUS_IMPLEMENTATION_SPEC.md) for technical details
- Review ESPHome documentation at https://esphome.io/

## Changelog

### Version 1.0.0 (Initial Release)

- Complete wMBUS Mode C receiver implementation
- CC1101 radio driver with SPI interface
- AES-128-CTR decryption using mbedTLS
- CRC-16-EN-13757-4 validation
- Support for compact and long frame formats
- Automatic radio health monitoring and recovery
- Home Assistant integration via ESPHome API
- Water consumption, temperature, and status monitoring

## Disclaimer

This component is provided as-is for educational and personal use. Ensure you have permission from your water utility to read your meter data. Some jurisdictions may have regulations regarding wireless meter reading.
