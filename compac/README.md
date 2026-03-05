# Compac CNG Dispenser Protocol Handler

A complete Modbus RTU protocol implementation for Compac CNG fuel dispenser systems with multi-hose support and BCD12 totalizer parsing.

## Quick Start

### 1. Add to Your Project

```bash
# Copy this directory to your ESP-IDF project
mkdir -p lib/dispenser_protocols/
cp -r compac_cng lib/dispenser_protocols/
```

### 2. Update CMakeLists.txt

```cmake
# In your main CMakeLists.txt or component CMakeLists.txt
IDFCOMPONENT_REGISTER(
    REQUIRES modbus_adapter
    # ... other requirements
)
```

### 3. Include Header

```c
#include "compac_protocol_handler.h"
```

### 4. Configure Device in iot_config.json

```json
{
  "ro_code": "COMPAC_CNG_PUMP_001_HOSE_1",
  "c_id": 4001,
  "c_make": "Compac_CNG_Dispenser",
  "post_interval": 30,
  "mbus_RTU": {
    "slave": 1,
    "baud": 9600,
    "dataBits": 8,
    "parity": "N",
    "stopBits": 1
  },
  "m": [256, 16]
}
```

## Key Features

| Feature | Details |
|---------|---------|
| **Protocol** | Modbus RTU (IEC 61158-2 standard) |
| **Transport** | RS-485 2-wire multi-drop |
| **Serial Params** | 9600-8N1 (no parity - critical!) |
| **Slave Addressing** | 01-99 (configurable per nozzle) |
| **Multi-Hose** | Each hose = independent Modbus slave |
| **Read Registers** | FC03 (function code) |
| **Write Registers** | FC06, FC16 (write single/multiple) |
| **Data Types** | U16, INT16, U32, **BCD12** |
| **Register Range** | 0x0100 - 0x0205 (25 registers) |

## Protocol Highlights

### No Parity (Critical Difference from Gasorex)

⚠️ Compac uses **no parity (N)**, not even parity. Ensure your ESP32 configuration is set to `"parity": "N"` in iot_config.json.

### BCD12 Totalizer Values

Compac totalizers (registers 0x0106-0x0108 and 0x0109-0x010B) use 6-byte binary-coded decimal encoding. Each register pair contains 2 BCD digits.

```c
// Example parsing
uint8_t bcd_bytes[6] = {0x12, 0x34, 0x56, 0x78, 0x90, 0x12};
char bcd_string[13];
compac_parse_bcd12(bcd_bytes, bcd_string);
// Result: "123456789012"
double quantity = compac_bcd12_to_double(bcd_string, 0.001);
// Result: 123456789.012 kg
```

### Multi-Nozzle Architecture

Unlike Gasorex (dual sides on one unit), Compac treats each hose as a separate Modbus slave. For a dual-hose dispenser:

```json
[
  {
    "ro_code": "STATION_A_HOSE_1",
    "c_id": 4001,
    "mbus_RTU": {"slave": 1, ...},
    "m": [256, 16]
  },
  {
    "ro_code": "STATION_A_HOSE_2",
    "c_id": 4002,
    "mbus_RTU": {"slave": 2, ...},
    "m": [256, 16]
  }
]
```

## Register Map Quick Reference

| Address | Type | Name | Unit | Scale | Access |
|---------|------|------|------|-------|--------|
| 0x0100 | U16 | Status Bits | - | - | RO |
| 0x0101 | U16 | Unit Price | Rs/kg | 0.01 | RO |
| 0x0102-03 | U32 | Qty Dispensed | kg | 0.001 | RO |
| 0x0104-05 | U32 | Amt Dispensed | Rs | 0.01 | RO |
| 0x0106-08 | BCD12 | Qty Totalizer | kg | 0.001 | RO |
| 0x0109-0B | BCD12 | Amt Totalizer | Rs | 0.01 | RO |
| 0x0112 | U16 | Pressure | bar | 0.01 | RO |
| 0x0113 | U16 | Flow Rate | kg/min | 0.01 | RO |
| 0x0114 | U16 | Error Bits | - | - | RO |
| 0x0115 | INT16 | Ambient Temp | °C | 0.1 | RO |
| 0x0116 | INT16 | Gas Temp | °C | 0.1 | RO |
| 0x0200 | U16 | Lock Control | - | - | RW |
| 0x0201 | U16 | Unit Price Set | Rs/kg | 0.01 | RW |
| 0x0202 | U16 | Target Pressure | bar | 0.01 | RW |
| 0x0203 | U16 | Max Pressure | bar | 0.01 | RW |
| 0x0204 | U16 | Flow Threshold | % | 1 | RW |
| 0x0205 | U16 | Min Flow Rate | kg/min | 0.01 | RW |

## API Examples

### Parse Complete Telemetry

```c
#include "compac_protocol_handler.h"

// Assuming buffer contains Modbus RTU response frame
uint8_t response_frame[37] = {...};  // [Slave] [FC] [ByteCount] [Data(32)] [CRC-L] [CRC-H]
compac_telemetry_t telemetry = {0};

int result = compac_parse_telemetry(response_frame, 37, &telemetry);

if (result == COMPAC_PARSE_OK && telemetry.data_valid) {
    printf("Quantity: %.3f kg\n", telemetry.delivery.quantity_kg);
    printf("Amount: %.2f Rs\n", telemetry.delivery.amount_currency);
    printf("Pressure: %.2f bar\n", telemetry.pressure.dispenser_pressure_bar);
    printf("Total Dispensed: %.3f kg\n", telemetry.totalizer.quantity_kg);
    printf("Error Count: %d\n", telemetry.errors.overpressure_warning ? 1 : 0);
}
```

### Parse Status Bits

```c
uint16_t status_raw = 0x0007;  // From register 0x0100
compac_status_bits_t status = {0};

compac_decode_status_bits(status_raw, &status);

if (status.mfm_running && status.bank1_active) {
    printf("Dispenser actively dispensing on low-pressure bank\n");
}
```

### Build Write Frame (Update Price)

```c
uint8_t frame_buffer[8];
size_t frame_length = sizeof(frame_buffer);

int result = compac_build_price_update_frame(
    1,      // slave address
    5000,   // price value (5000 × 0.01 = 50.00 Rs)
    frame_buffer,
    &frame_length
);

if (result == COMPAC_BUILD_OK) {
    // Send frame_buffer (8 bytes) over RS-485
    rs485_send(frame_buffer, frame_length);
}
```

### CRC Calculation

```c
uint8_t data[] = {0x01, 0x03, 0x01, 0x00, 0x00, 0x01};
uint16_t crc = compac_crc16_modbus(data, 6);
printf("CRC: 0x%04X\n", crc);  // Expected: 0x0A84
```

## File Structure

```
compac_cng/
├── PROTOCOL.md                     # Complete technical specification
├── register_map.csv                # Register reference table
├── CLOUD_IMPLEMENTATION.md         # Cloud parsing (4 languages)
├── DEPLOYMENT_CHECKLIST.md         # Production deployment guide
├── compac_protocol_handler.h       # Public API header
├── compac_protocol_handler.c       # Implementation
├── CMakeLists.txt                  # ESP-IDF component build
├── library.json                    # PlatformIO metadata
├── sample_config_single_hose.json  # 1-hose config
├── sample_config_dual_hose.json    # 2-hose config
├── sample_config_quad_hose.json    # 4-hose config
└── README.md                       # This file
```

## Polling Strategy

The system reads **16 consecutive registers** (0x0100-0x010F) to gather complete telemetry:

```
Registers Read: 0x0100-0x010F
Frame Size: 1 (slave) + 1 (FC) + 1 (bytecount) + 32 (data) + 2 (CRC) = 37 bytes
Typical Response Time: 30-50 ms
Minimum Polling Interval: 100 ms (Compac specification)
Recommended Interval: 30 seconds (for station telemetry)
```

For multi-hose dispensers, ensure polling to different slaves is spaced ≥100ms apart:

```
T=0ms:   Poll Slave 1 (Hose 1)
T=50ms:  Response from Slave 1
T=100ms: Poll Slave 2 (Hose 2)
T=150ms: Response from Slave 2
T=160ms: Publish telemetry for both hoses
```

## Error Codes

| Code | Meaning | Action |
|------|---------|--------|
| `COMPAC_PARSE_OK` | Success | Data is valid |
| `COMPAC_PARSE_INVALID_CRC` | CRC mismatch | Check RS-485 connection |
| `COMPAC_PARSE_INVALID_FC` | Not FC03 response | Verify dispenser response |
| `COMPAC_PARSE_SHORT_FRAME` | <35 bytes | Incomplete response; check timeout |
| `COMPAC_PARSE_BCD_ERROR` | Invalid BCD digit | Frame corruption; check parity (must be N) |

## Troubleshooting

### No Response from Dispenser
1. Verify RS-485 physical connection (A/B pair)
2. Check slave address (physical switch inside dispenser)
3. Confirm baud rate: 9600 (fixed in Compac)
4. **Verify parity: "N" (no parity)** - most common error!
5. Use Modbus RTU analyzer to test directly

### BCD12 Parse Fails
- Indicates frame corruption (CRC passed but nibbles invalid)
- Check RS-485 termination (120Ω at both ends)
- Check cable shielding for EMI
- Reduce polling rate if temporarily stuck

### Pressure Values Unrealistic
- Verify scale factor: `pressure_raw × 0.01`
- Typical range: 0-655.35 bar
- Temperature range: -327.68 to 327.67 °C

### Totalizer Always Zero
- BCD parsing must succeed (not INT or U32 interpretation)
- Use `compac_parse_bcd12()` function
- Check if register address 0x0106 is actually responding
- If always zero, dispenser may be offline

## Cloud Integration

See `CLOUD_IMPLEMENTATION.md` for:
- JavaScript/Node.js parser
- Python parser
- C# parser
- Azure Stream Analytics queries
- Cosmos DB / Data Lake integration

## Deployment Guide

See `DEPLOYMENT_CHECKLIST.md` for:
- Pre-deployment hardware validation
- Firmware build steps
- Configuration generation
- First data verification
- Production monitoring setup
- Troubleshooting decision trees
- Rollback procedures

## References

- **Technical Specification:** `PROTOCOL.md`
- **Register Details:** `register_map.csv`
- **Cloud Parsing:** `CLOUD_IMPLEMENTATION.md`
- **Deployment:** `DEPLOYMENT_CHECKLIST.md`
- **Modbus RTU Standard:** IEC 61158-2
- **RS-485 Standard:** EIA/TIA-485-A
- **BCD Encoding:** ISO 6093

## Version History

| Version | Date | Notes |
|---------|------|-------|
| 1.0.0 | 2026-03-05 | Initial release for Compac CNG with BCD12 support |

## Support

For issues or enhancements:
1. Check `DEPLOYMENT_CHECKLIST.md` troubleshooting section
2. Verify parity is "N" (most common error)
3. Capture raw Modbus frame with analyzer
4. Review `PROTOCOL.md` register definitions

---

**Manufacturer:** Compac  
**Application:** CNG Fuel Dispenser Monitoring  
**Protocol:** Modbus RTU (IEC 61158-2)  
**Last Updated:** 2026-03-05

