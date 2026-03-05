# Gasorex SR Energy - Implementation Guide for Cloud Backend

## Overview

This document provides guidance for implementing cloud-side (Azure backend) logic to parse raw Modbus telemetry from the Gasorex SR Energy dispenser system and convert it into actionable business metrics.

---

## 1. Raw Telemetry Format from ESP32

The ESP32 publishes telemetry JSON to Azure IoT Hub in this format:

```json
{
  "timestamp": "2025-03-05T14:30:45Z",
  "reg_data": [
    {
      "addr": 1,
      "len": 14,
      "data": "4201000041800000414000004100000041c0000041a000000000000000000000060000000"
    },
    {
      "addr": 16,
      "len": 15,
      "data": "4200800041a0000041900000414000004060000040c0000041400000000000000600000000000"
    }
  ]
}
```

**Field Definitions:**
- `timestamp`: ISO-8601 datetime of telemetry collection
- `reg_data`: Array of register ranges
  - `addr`: Starting Modbus register address (1 for Side A, 16 for Side B)
  - `len`: Number of consecutive registers read
  - `data`: Hex-encoded binary payload (each pair of hex digits = 1 byte)

---

## 2. Parsing Strategy

### Step 1: Convert Hex String to Binary Buffer

```javascript
// JavaScript
const hexString = "4201000041800000...";
const buffer = Buffer.from(hexString, 'hex');

// Python
import binascii
hex_string = "4201000041800000..."
buffer = binascii.unhexlify(hex_string)

// C#
string hexString = "4201000041800000...";
byte[] buffer = Convert.FromHexString(hexString);
```

### Step 2: Parse Big-Endian Float32 (IEEE-754)

**JavaScript:**
```javascript
function parseFloat32BE(buffer, offset) {
    return buffer.readFloatBE(offset);
}

// Or manually:
function parseFloat32BEManual(buffer, offset) {
    const bytes = [
        buffer[offset],
        buffer[offset + 1],
        buffer[offset + 2],
        buffer[offset + 3]
    ];
    const view = new DataView(new Uint8Array(bytes).buffer);
    return view.getFloat32(0, false);  // false = big-endian
}
```

**Python:**
```python
import struct

def parse_float32_be(data, offset):
    return struct.unpack('>f', data[offset:offset+4])[0]  # >f = big-endian float
```

**C#:**
```csharp
private static float ParseFloat32BE(byte[] data, int offset)
{
    if (BitConverter.IsLittleEndian)
        System.Array.Reverse(data, offset, 4);
    return BitConverter.ToSingle(data, offset);
}
```

### Step 3: Parse Big-Endian Integers

**JavaScript:**
```javascript
function parseUInt16BE(buffer, offset) {
    return buffer.readUInt16BE(offset);
}

function parseInt16BE(buffer, offset) {
    return buffer.readInt16BE(offset);
}
```

**Python:**
```python
def parse_uint16_be(data, offset):
    return int.from_bytes(data[offset:offset+2], 'big')

def parse_int16_be(data, offset):
    return int.from_bytes(data[offset:offset+2], 'big', signed=True)
```

**C#:**
```csharp
private static ushort ParseUInt16BE(byte[] data, int offset)
{
    return (ushort)((data[offset] << 8) | data[offset + 1]);
}

private static short ParseInt16BE(byte[] data, int offset)
{
    return (short)((data[offset] << 8) | data[offset + 1]);
}
```

---

## 3. Complete Parsing Function

### JavaScript Implementation

```javascript
class GasorexParser {
    static parseRegData(regDataArray) {
        if (!Array.isArray(regDataArray) || regDataArray.length < 2) {
            throw new Error('Expected at least 2 register ranges (Side A and Side B)');
        }

        const sideAData = Buffer.from(regDataArray[0].data, 'hex');
        const sideBData = Buffer.from(regDataArray[1].data, 'hex');
        
        return {
            side_a: this.parseSide(sideAData, 'A'),
            side_b: this.parseSide(sideBData, 'B'),
            pricing: regDataArray.length >= 3 ? 
                this.parsePricing(Buffer.from(regDataArray[2].data, 'hex')) : 
                { first_rate_rs_per_kg: 0, second_rate_rs_per_kg: 0 }
        };
    }

    static parseSide(buffer, side) {
        if (buffer.length < 28) {
            throw new Error(`Insufficient data for side ${side}: expected 28 bytes, got ${buffer.length}`);
        }

        let offset = 0;
        const side_data = {
            side: side,
            amount_rs: this.readFloat32BE(buffer, offset),
            quantity_kg: this.readFloat32BE(buffer, offset += 4),
            electronic_totalizer_rs: this.readFloat32BE(buffer, offset += 4),
            flowmeter_totalizer_kg: this.readFloat32BE(buffer, offset += 4),
            pt1_pressure_bar: buffer.readUInt16BE(offset += 4) * 0.1,
            pt2_pressure_bar: buffer.readUInt16BE(offset += 2) * 0.1,
            flowrate_kg_per_min: this.readFloat32BE(buffer, offset += 2),
            error_status_flags: this.decodeErrorFlags(buffer.readUInt16BE(offset += 4)),
            emergency_stop_active: buffer.readUInt16BE(offset += 2) !== 0,
            temperature_celsius: buffer.length > offset + 2 ? 
                this.readInt16BE(buffer, offset + 2) * 0.1 : null
        };
        
        return side_data;
    }

    static parsePricing(buffer) {
        if (buffer.length < 8) return { first_rate_rs_per_kg: 0, second_rate_rs_per_kg: 0 };
        
        return {
            first_rate_rs_per_kg: this.readFloat32BE(buffer, 0),
            second_rate_rs_per_kg: this.readFloat32BE(buffer, 4)
        };
    }

    static readFloat32BE(buffer, offset) {
        return buffer.readFloatBE(offset);
    }

    static readInt16BE(buffer, offset) {
        return buffer.readInt16BE(offset);
    }

    static decodeErrorFlags(statusWord) {
        return {
            flowmeter_error: (statusWord & 0x0001) !== 0,
            pt_error: (statusWord & 0x0002) !== 0,
            solenoid_fault: (statusWord & 0x0004) !== 0,
            safety_interlock: (statusWord & 0x0008) !== 0,
            overtemp_warning: (statusWord & 0x0010) !== 0,
            underpressure_warning: (statusWord & 0x0020) !== 0,
            overpressure_warning: (statusWord & 0x0040) !== 0,
            control_module_fault: (statusWord & 0x0080) !== 0
        };
    }
}

// Usage:
const telemetry = {
    timestamp: "2025-03-05T14:30:45Z",
    reg_data: [ /* ... from IoT Hub message ... */ ]
};

const parsed = GasorexParser.parseRegData(telemetry.reg_data);
console.log('Side A:', parsed.side_a);
console.log('Side B:', parsed.side_b);
```

### Python Implementation

```python
import struct
from dataclasses import dataclass
from typing import List, Dict, Any

@dataclass
class ErrorFlags:
    flowmeter_error: bool
    pt_error: bool
    solenoid_fault: bool
    safety_interlock: bool
    overtemp_warning: bool
    underpressure_warning: bool
    overpressure_warning: bool
    control_module_fault: bool

@dataclass
class SideData:
    side: str
    amount_rs: float
    quantity_kg: float
    electronic_totalizer_rs: float
    flowmeter_totalizer_kg: float
    pt1_pressure_bar: float
    pt2_pressure_bar: float
    flowrate_kg_per_min: float
    error_status_flags: ErrorFlags
    emergency_stop_active: bool
    temperature_celsius: float

@dataclass
class GasorexTelemetry:
    side_a: SideData
    side_b: SideData
    first_rate_rs_per_kg: float
    second_rate_rs_per_kg: float

class GasorexParser:
    @staticmethod
    def parse_float32_be(data: bytes, offset: int) -> float:
        return struct.unpack('>f', data[offset:offset+4])[0]
    
    @staticmethod
    def parse_int16_be(data: bytes, offset: int) -> int:
        return int.from_bytes(data[offset:offset+2], 'big', signed=True)
    
    @staticmethod
    def parse_uint16_be(data: bytes, offset: int) -> int:
        return int.from_bytes(data[offset:offset+2], 'big')
    
    @staticmethod
    def decode_error_flags(status_word: int) -> ErrorFlags:
        return ErrorFlags(
            flowmeter_error=(status_word & 0x0001) != 0,
            pt_error=(status_word & 0x0002) != 0,
            solenoid_fault=(status_word & 0x0004) != 0,
            safety_interlock=(status_word & 0x0008) != 0,
            overtemp_warning=(status_word & 0x0010) != 0,
            underpressure_warning=(status_word & 0x0020) != 0,
            overpressure_warning=(status_word & 0x0040) != 0,
            control_module_fault=(status_word & 0x0080) != 0
        )
    
    @staticmethod
    def parse_side(data: bytes, side: str) -> SideData:
        if len(data) < 28:
            raise ValueError(f'Insufficient data for side {side}: got {len(data)} bytes')
        
        offset = 0
        return SideData(
            side=side,
            amount_rs=GasorexParser.parse_float32_be(data, offset),
            quantity_kg=GasorexParser.parse_float32_be(data, offset + 4),
            electronic_totalizer_rs=GasorexParser.parse_float32_be(data, offset + 8),
            flowmeter_totalizer_kg=GasorexParser.parse_float32_be(data, offset + 12),
            pt1_pressure_bar=GasorexParser.parse_uint16_be(data, offset + 16) * 0.1,
            pt2_pressure_bar=GasorexParser.parse_uint16_be(data, offset + 18) * 0.1,
            flowrate_kg_per_min=GasorexParser.parse_float32_be(data, offset + 20),
            error_status_flags=GasorexParser.decode_error_flags(
                GasorexParser.parse_uint16_be(data, offset + 24)
            ),
            emergency_stop_active=GasorexParser.parse_uint16_be(data, offset + 26) != 0,
            temperature_celsius=GasorexParser.parse_int16_be(data, offset + 28) * 0.1 
                if len(data) > 30 else None
        )
    
    @staticmethod
    def parse_reg_data(reg_data_array: List[Dict[str, Any]]) -> GasorexTelemetry:
        if len(reg_data_array) < 2:
            raise ValueError('Expected at least 2 register ranges')
        
        side_a_buf = bytes.fromhex(reg_data_array[0]['data'])
        side_b_buf = bytes.fromhex(reg_data_array[1]['data'])
        
        first_rate, second_rate = 0.0, 0.0
        if len(reg_data_array) >= 3:
            pricing_buf = bytes.fromhex(reg_data_array[2]['data'])
            first_rate = GasorexParser.parse_float32_be(pricing_buf, 0)
            second_rate = GasorexParser.parse_float32_be(pricing_buf, 4)
        
        return GasorexTelemetry(
            side_a=GasorexParser.parse_side(side_a_buf, 'A'),
            side_b=GasorexParser.parse_side(side_b_buf, 'B'),
            first_rate_rs_per_kg=first_rate,
            second_rate_rs_per_kg=second_rate
        )

# Usage:
telemetry = {
    "timestamp": "2025-03-05T14:30:45Z",
    "reg_data": [ # ... from IoT Hub ... ]
}
parsed = GasorexParser.parse_reg_data(telemetry['reg_data'])
print(f'Side A Amount: {parsed.side_a.amount_rs} Rs')
print(f'Side B Flowrate: {parsed.side_b.flowrate_kg_per_min} kg/min')
```

---

## 4. Azure Function Integration (JavaScript)

```javascript
module.exports = async function(context, iotHubMessage) {
    context.log('Processing Gasorex telemetry');
    
    try {
        const parsed = GasorexParser.parseRegData(iotHubMessage.reg_data);
        
        // Store in Cosmos DB
        const document = {
            id: context.executionContext.invocationId,
            deviceId: iotHubMessage.deviceId,
            timestamp: new Date(iotHubMessage.timestamp),
            side_a: parsed.side_a,
            side_b: parsed.side_b,
            pricing: parsed.pricing,
            
            // Derived metrics
            total_amount_rs: parsed.side_a.amount_rs + parsed.side_b.amount_rs,
            total_quantity_kg: parsed.side_a.quantity_kg + parsed.side_b.quantity_kg,
            
            // Alerts
            alerts: [
                ...parseAlerts(parsed.side_a, 'A'),
                ...parseAlerts(parsed.side_b, 'B')
            ]
        };
        
        context.bindings.cosmosDb = document;
        
        // Check for critical alerts
        if (document.alerts.some(a => a.severity === 'critical')) {
            context.bindings.alertQueue = document;
        }
        
        context.res = { body: 'Processed successfully' };
    } catch (error) {
        context.log.error(`Error parsing telemetry: ${error.message}`);
        context.res = { status: 400, body: error.message };
    }
};

function parseAlerts(sideData, side) {
    const alerts = [];
    
    if (sideData.error_status_flags.flowmeter_error) {
        alerts.push({
            side, severity: 'critical', type: 'Flowmeter Error',
            message: `Side ${side} flowmeter disconnected or failed`
        });
    }
    if (sideData.error_status_flags.overpressure_warning) {
        alerts.push({
            side, severity: 'warning', type: 'Overpressure',
            message: `Side ${side} pressure at ${sideData.pt1_pressure_bar} bar`
        });
    }
    if (sideData.error_status_flags.underpressure_warning) {
        alerts.push({
            side, severity: 'warning', type: 'Underpressure',
            message: `Side ${side} pressure dropped to ${sideData.pt1_pressure_bar} bar`
        });
    }
    if (sideData.emergency_stop_active) {
        alerts.push({
            side, severity: 'warning', type: 'Emergency Stop',
            message: `Side ${side} emergency stop is active`
        });
    }
    
    return alerts;
}
```

---

## 5. Stream Analytics Query

For real-time aggregation:

```sql
WITH ParsedData AS (
    SELECT
        IoTHub.ConnectionDeviceId as device_id,
        System.Timestamp as event_time,
        
        -- Parse JSON (requires AS JSON function)
        JSON_VALUE(telemetry, '$.side_a.amount_rs') as side_a_amount,
        JSON_VALUE(telemetry, '$.side_a.flowrate_kg_per_min') as side_a_flowrate,
        JSON_VALUE(telemetry, '$.side_b.amount_rs') as side_b_amount,
        JSON_VALUE(telemetry, '$.side_b.flowrate_kg_per_min') as side_b_flowrate
    FROM
        input_iot i
        CROSS APPLY GetRecordProperties(i) AS r
)
SELECT
    device_id,
    event_time,
    side_a_amount,
    side_a_flowrate,
    side_b_amount,
    side_b_flowrate,
    CAST(side_a_amount AS float) + CAST(side_b_amount AS float) as total_amount_rs,
    CASE
        WHEN CAST(side_a_flowrate AS float) > 400 OR CAST(side_b_flowrate AS float) > 400 
            THEN 'WARNING_FLOWRATE'
        ELSE 'OK'
    END as status
INTO
    output_metrics
FROM
    ParsedData
```

---

## 6. Testing

### Unit Test (JavaScript - Jest)

```javascript
test('Parse Gasorex telemetry correctly', () => {
    const input = {
        reg_data: [
            {
                addr: 1,
                len: 14,
                data: '420100000000000042000000...', // Example hex data
            },
            {
                addr: 16,
                len: 15,
                data: '420280000000000042000000...'
            }
        ]
    };

    const result = GasorexParser.parseRegData(input.reg_data);
    
    expect(result.side_a).toBeDefined();
    expect(result.side_b).toBeDefined();
    expect(typeof result.side_a.amount_rs).toBe('number');
    expect(result.side_a.error_status_flags).toHaveProperty('flowmeter_error');
});

test('Decode error flags correctly', () => {
    const statusWord = 0x0007; // Bits 0, 1, 2 set
    const flags = GasorexParser.decodeErrorFlags(statusWord);
    
    expect(flags.flowmeter_error).toBe(true);
    expect(flags.pt_error).toBe(true);
    expect(flags.solenoid_fault).toBe(true);
    expect(flags.safety_interlock).toBe(false);
});
```

---

## 7. Production Checklist

- [ ] Test with real GSR-CU-23 hardware and verify data accuracy
- [ ] Implement error handling for malformed hex data
- [ ] Add logging for parsing errors and anomalies
- [ ] Set up alerts for critical errors (flowmeter failure, overpressure)
- [ ] Create dashboards for Side A/B metrics in Power BI or similar
- [ ] Document scaling factors and units in data dictionary
- [ ] Verify pricing calculation: `total_rs = quantity_kg × rate_rs_per_kg`
- [ ] Archive telemetry for compliance/auditing (30+ days typical)
- [ ] Test emergency stop control channel (if implemented)

---

## References

- [Gasorex PROTOCOL.md](PROTOCOL.md) - Full protocol specification
- IEEE 754: Single-precision floating-point format
- Modbus RTU Standard: IEC 61158-2
