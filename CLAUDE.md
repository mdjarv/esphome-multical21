- use `python -m esphome` to run esphome commands

## Reference Materials

### WMBUS_IMPLEMENTATION_SPEC.md
- Complete specification for wMBUS receiver implementation with ESP32-C3 + CC1101
- This document contains the authoritative protocol details, timing requirements, and hardware configuration
- Follow this specification when implementing packet reception, decryption, and processing

### ../esp-multical21 Directory
- Working reference implementation (non-ESPHome)
- Successfully receives, decodes, and posts data from the Multical21 water meter
- Use as reference for protocol handling, but NOT for ESPHome component architecture
- Goal: Port this functionality to ESPHome framework following ESPHome best practices