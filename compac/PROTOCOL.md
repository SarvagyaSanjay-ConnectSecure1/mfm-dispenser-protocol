# Compac CNG Dispenser System Protocol Specification

## 1. Overview

**Manufacturer:** Compac  
**Product:** CNG (Compressed Natural Gas) Dispenser System  
**Communication Protocol:** Modbus RTU (RS-485 serial)  
**Application:** Multi-hose CNG fuel dispensing with pressure regulation  
**Primary Use Case:** Compressed Natural Gas (CNG) vehicle refueling stations  

---

## 2. Key Architectural Characteristics

### Multi-Nozzle Design

Unlike single-unit systems, **each dispenser hose/nozzle is an independent Modbus slave device**:

```
┌─────────────────────────────────────────────┐
│    Compac CNG Dispenser Unit (Physical)     │
├─────────────────────────────────────────────┤
│                                             │
│  ┌──────────────┐  ┌──────────────┐       │
│  │ Hose 1       │  │ Hose 2       │ ...   │
│  │ (Nozzle 1)   │  │ (Nozzle 2)   │       │
│  │ Slave ID: 01 │  │ Slave ID: 02 │       │
│  └──────────────┘  └──────────────┘       │
│         ↓                  ↓               │
│         └──────┬───────────┘               │
│                ↓                          │
│         ┌────────────────┐                │
│         │ RS-485 Common  │                │
│         │ (A/B diff pair)│                │
│         └────────────────┘                │
│                                             │
└─────────────────────────────────────────────┘
                     ↓
          SCADA / Monitoring System
          (reads from each slave separately)
```

**Implication:** To monitor a dual-hose dispenser, create **two separate device configurations** in the ESP32, one for each hose with different slave addresses.

---

## 3. Physical & Communication Layer

### 3.1 Serial Parameters

| Parameter | Specification | Notes |
|-----------|---------------|-------|
| **Baud Rate** | 9600 bps | Fixed, non-configurable |
| **Data Bits** | 8 | Standard for Modbus |
| **Parity** | **None (N)** | ⚠️ Different from Gasorex (which uses Even) |
| **Stop Bits** | 1 | Standard for Modbus |
| **Protocol** | Modbus RTU | IEC 61158-2 compliant |
| **Transmission** | Half-duplex | RS-485 differential lines |

### 3.2 RS-485 Physical Requirements

| Aspect | Specification |
|--------|---------------|
| **Line Pair** | 2-wire differential (A and B) |
| **Termination** | 120 Ω resistor at each end of bus |
| **Max Cable Length** | 1200 m (typical industrial) |
| **Max Devices** | 32 devices per RS-485 segment (Modbus spec) |
| **Polling Interval** | **Minimum 100 ms between consecutive requests** |

⚠️ **Critical:** Compac specifications mandate a 100 ms minimum interval. Faster polling may cause communication errors.

### 3.3 Slave Addressing

**Configurable via parameter switch inside the dispenser:**
- **Slave Address Range:** 01 to 99
- **Default (common):** Slave 01 for first hose, 02 for second, etc.
- **Configuration Method:** Physical switch inside dispenser cabinet (refer to Compac manual)

---

## 4. Modbus RTU Frame Structure

### 4.1 Request Frame (Read Registers - Function Code 03)

```
[Slave ID] [Function Code] [Register Addr H] [Register Addr L] 
[Quantity H] [Quantity L] [CRC-Lo] [CRC-Hi]

1 byte       1 byte            1 byte         1 byte
1 byte       1 byte            1 byte        1 byte
```

**Example: Read registers 0100–0103 (2 registers)**
```
Byte 0: 0x01        (Slave ID = 1)
Byte 1: 0x03        (Function Code = Read Holding Registers)
Byte 2: 0x01        (Register Address High = 0x0100)
Byte 3: 0x00        (Register Address Low)
Byte 4: 0x00        (Quantity High = 2 registers)
Byte 5: 0x02        (Quantity Low)
Byte 6: 0x?? 		(CRC Low)
Byte 7: 0x??        (CRC High)
```

### 4.2 Response Frame (Read Registers - Function Code 03)

```
[Slave ID] [Function Code] [Byte Count] [Register Data...] [CRC-Lo] [CRC-Hi]
1 byte     1 byte          1 byte       N bytes           1 byte   1 byte
```

**Example Response (2 registers, 4 bytes):**
```
Byte 0: 0x01        (Slave ID)
Byte 1: 0x03        (Function Code)
Byte 2: 0x04        (Byte Count = 4 bytes)
Byte 3: 0xAB        (Register 0100 High)
Byte 4: 0xCD        (Register 0100 Low)
Byte 5: 0xEF        (Register 0101 High)
Byte 6: 0x12        (Register 0101 Low)
Byte 7: 0x?? 		(CRC Low)
Byte 8: 0x??        (CRC High)
```

### 4.3 Write Operations

**Function Code 06 (Write Single Register):**
```
[Slave] [FC=06] [Reg Addr H] [Reg Addr L] [Value H] [Value L] [CRC-Lo] [CRC-Hi]
```

**Function Code 16 (Write Multiple Registers):**
```
[Slave] [FC=16] [Reg Addr H] [Reg Addr L] [Qty H] [Qty L] [Byte Count] [Data...] [CRC]
```

---

## 5. Complete Register Map (0100–0205)

### 5.1 Status & Operation Registers (0100–0101)

| Addr | FC | Type | Name | Unit | Scale | Access | Description |
|------|----|----|------|------|-------|--------|-------------|
| **0100** | 3 | U16 | Status Bits | - | - | RO | Push-to-start status, stop button, MFM status, trip, bank activation |
| **0101** | 3 | U16 | Unit Price | Currency | 0.01 | RO | Current unit price (local currency per unit mass) |

**Register 0100 Status Bit Layout:**
```
Bit 0: Push-to-start push button status (1=pressed, 0=released)
Bit 1: Stop button status (1=stopped, 0=running)
Bit 2: Mass Flow Meter status (1=running, 0=stopped)
Bit 3: Dispenser tripping status (1=tripped, 0=normal)
Bit 4: Bank 1 (Low pressure) activation (1=active, 0=inactive)
Bit 5: Bank 2 (Medium pressure) activation (1=active, 0=inactive)
Bit 6: Bank 3 (High pressure) activation (1=active, 0=inactive)
Bits 7–15: Reserved (0)
```

---

### 5.2 Delivery Information (0102–0105)

| Addr | FC | Type | Name | Unit | Scale | Access | Description |
|------|----|----|------|------|-------|--------|-------------|
| **0102–0103** | 3 | U32 | Current Delivery Quantity | kg | 0.001 | RO | Quantity dispensed in current transaction (32-bit, 3 decimal places) |
| **0104–0105** | 3 | U32 | Current Delivery Amount | Currency | 0.01 | RO | Monetary value of current transaction (32-bit, 2 decimal places) |

**Data Format:** Big-endian 32-bit (MSW first, LSW second)
```
Registers 0102–0103 combined form 32-bit quantity
Registers 0104–0105 combined form 32-bit monetary amount
```

---

### 5.3 Totalizer Information (0106–0111)

| Addr | FC | Type | Name | Unit | Scale | Access | Description |
|------|----|----|------|------|-------|--------|-------------|
| **0106–0108** | 3 | BCD12 | Total Hose Quantity Totalizer | kg | 0.001 | RO | Cumulative quantity on this hose (6-byte BCD) |
| **0109–0111** | 3 | BCD12 | Total Hose Monetary Totalizer | Currency | 0.01 | RO | Cumulative monetary value on this hose (6-byte BCD) |

**BCD12 Format (Important - 6 bytes):**
```
Byte 0: Highest order digits (BCD)
Byte 1: Next pair of digits (BCD)
Byte 2: Next pair of digits (BCD)
Byte 3: Next pair of digits (BCD)
Byte 4: Next pair of digits (BCD)
Byte 5: Lowest order digits (BCD)

Example: Value 123456.78 in BCD12:
Byte 0: 0x12 (digits "12")
Byte 1: 0x34 (digits "34")
Byte 2: 0x56 (digits "56")
Byte 3: 0x78 (digits "78")
Byte 4: 0x00 
Byte 5: 0x00
= "123456.78"
```

---

### 5.4 Process Parameters (0112–0116)

| Addr | FC | Type | Name | Unit | Scale | Access | Description |
|------|----|----|------|------|-------|--------|-------------|
| **0112** | 3 | U16 | Dispenser Pressure | bar | 0.01 | RO | Current gas pressure in dispenser outlet (2 decimal places) |
| **0113** | 3 | U16 | Flow Rate | kg/min | 0.01 | RO | Current gas flow rate (2 decimal places) |
| **0114** | 3 | U16 | Error Status Bits | - | - | RO | Error and fault flags (see table below) |
| **0115** | 3 | INT16 | Ambient Temperature | °C | 0.1 | RO | External/ambient temperature (signed, 1 decimal place) |
| **0116** | 3 | INT16 | Gas Temperature | °C | 0.1 | RO | Gas temperature at dispenser outlet (signed, 1 decimal place) |

**Register 0114 Error Status Bit Layout:**
```
Bit 0: Reverse flow detected (1=error, 0=normal)
Bit 1: Mass flow meter fault (1=error, 0=normal)
Bit 2: Temperature/Pressure board disconnected (1=error, 0=normal)
Bit 3: Pressure probe fault (1=error, 0=normal)
Bit 4: Temperature probe fault (1=error, 0=normal)
Bit 5: Dispenser overpressure (1=warning, 0=normal)
Bit 6: Dispenser underpressure (1=warning, 0=normal)
Bits 7–15: Reserved (0)
```

---

### 5.5 Control & Configuration Registers (0200–0205)

| Addr | FC | Type | Name | Unit | Access | Description |
|------|----|----|------|------|--------|-------------|
| **0200** | 3 | U16 | Dispenser Lock Control | - | RW | 0=free/unlocked, 1=locked (prevents operation) |
| **0201** | 3 | U16 | Unit Price Setting | Currency | RW | New unit price (applied when dispenser idle) |
| **0202** | 3 | U16 | Target Fill Pressure | bar | RW | Desired final pressure (scale 0.01) |
| **0203** | 3 | U16 | Maximum Fill Pressure | bar | RW | Safety limit for maximum pressure (scale 0.01) |
| **0204** | 3 | U16 | Bank Sequencing Param 1 | % | RW | Flow percentage threshold for bank transitions |
| **0205** | 3 | U16 | Bank Sequencing Param 2 | kg/min | RW | Minimum flow rate for high pressure bank (scale 0.01) |

---

## 6. Data Type Handling

### 6.1 16-bit Unsigned (U16)

**Big-endian format:**
```c
uint16_t value = (buf[0] << 8) | buf[1];
```

**Example:** Register 0100 with response bytes [0xAB, 0xCD]
```
value = 0xABCD = 43981 (decimal)
```

---

### 6.2 16-bit Signed (INT16)

**Two's complement, big-endian:**
```c
int16_t value = (int16_t)((buf[0] << 8) | buf[1]);
```

**Example:** Temperature register 0115 with bytes [0xFF, 0xF6]
```
Combined: 0xFFF6
As signed: -10 (decimal)
With scale 0.1: -10 × 0.1 = -1.0 °C
```

---

### 6.3 32-bit Unsigned (U32)

**Big-endian, split across 2 registers:**
```c
uint32_t value = ((buf[0] << 24) | (buf[1] << 16) | 
                  (buf[2] << 8) | buf[3]);
```

**Register Combination:**
- Registers 0102–0103 for quantity: High word at 0102, low word at 0103
- Registers 0104–0105 for amount: High word at 0104, low word at 0105

**Example:** Quantity at 0102–0103 with bytes [0x00, 0x01, 0x86, 0xA0]
```
High word (0102): 0x0001
Low word (0103): 0x86A0
Combined: 0x000186A0 = 100000 (register units)
With scale 0.001: 100000 × 0.001 = 100.0 kg
```

---

### 6.4 BCD12 (6-Byte Binary-Coded Decimal)

**Most critical unique feature of Compac protocol**

Each byte contains 2 BCD digits. 6 bytes = 12 digits total.

```c
// Parse BCD12
char bcd_string[13] = {0};
for (int i = 0; i < 6; i++) {
    uint8_t byte = buf[i];
    uint8_t high = (byte >> 4) & 0x0F;
    uint8_t low = byte & 0x0F;
    sprintf(&bcd_string[i*2], "%d%d", high, low);
}
// bcd_string now contains 12 ASCII digits
// Example: "000123456789"

// Convert to numeric value with scaling
uint64_t numeric = strtoull(bcd_string, NULL, 10);
double value = numeric * 0.001;  // For quantity (scale 0.001)
// Result: 123456.789 kg
```

**Register Examples:**

**Registers 0106–0108 (Quantity Totalizer - BCD12):**
```
Byte 0: 0x12 (digits "12")
Byte 1: 0x34 (digits "34")
Byte 2: 0x56 (digits "56")
Byte 3: 0x78 (digits "78")
Byte 4: 0x90 (digits "90")
Byte 5: 0x12 (digits "12")

Combined: "123456789012"
With scale 0.001: 123456789.012 kg (total dispensed on this hose)
```

**Registers 0109–0111 (Monetary Totalizer - BCD12):**
```
Same structure, but interpret with scale 0.01 for currency
Example "000000012345" → 0.01 × 12345 = 123.45 (currency units)
```

⚠️ **Important:** BCD parsing is mandatory for accurate totalizer values. Integer parsing will produce incorrect results.

---

## 7. CRC-16 Calculation

**Modbus RTU standard CRC-16 (polynomial 0xA001, reflected):**

```c
uint16_t crc16_modbus(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}
```

---

## 8. Communication Examples

### 8.1 Example 1: Read Status (Register 0100)

**Request (Read 1 register at 0100):**
```
Byte 0: 0x01        (Slave 1)
Byte 1: 0x03        (FC03: Read)
Byte 2: 0x01        (Address high)
Byte 3: 0x00        (Address low = 0x0100)
Byte 4: 0x00        (Quantity high)
Byte 5: 0x01        (Quantity low = 1)
CRC: Calculate...
Byte 6: 0x84        (CRC low)
Byte 7: 0x0A        (CRC high)
```

**Response (Status = 0x0007, Bank1 & Bank2 active):**
```
Byte 0: 0x01        (Slave)
Byte 1: 0x03        (FC03)
Byte 2: 0x02        (Byte count = 2)
Byte 3: 0x00        (Register value high)
Byte 4: 0x07        (Register value low)
Byte 5: 0xB8        (CRC low)
Byte 6: 0x44        (CRC high)

Status bits decoded:
- Bit 0 (0x01): Push-to-start = 1 (pressed)
- Bit 1 (0x02): Stop button = 1 (stopped)
- Bit 2 (0x04): MFM status = 1 (running)
```

---

### 8.2 Example 2: Read Quantity (Registers 0102–0103)

**Request (Read 2 registers):**
```
Byte 0: 0x01
Byte 1: 0x03
Byte 2: 0x01
Byte 3: 0x02        (Start address = 0x0102)
Byte 4: 0x00
Byte 5: 0x02        (Quantity = 2)
Byte 6: 0xB8
Byte 7: 0x4D
```

**Response (Quantity = 50.5 kg):**
```
Byte 0: 0x01
Byte 1: 0x03
Byte 2: 0x04        (Byte count = 4)
Byte 3: 0x00        (Register 0102 high)
Byte 4: 0xC6        (Register 0102 low)
Byte 5: 0x1C        (Register 0103 high)
Byte 6: 0x40        (Register 0103 low)
Byte 7: 0x??
Byte 8: 0x??        (CRC)

Combined: 0x00C61C40 = 205652544 (register units)
With scale 0.001: 205652.544 kg (incorrect scaling example)
```

---

### 8.3 Example 3: Write Unit Price (Register 0201)

**Request (Write 5000 = 50.00 currency with scale 0.01):**
```
Byte 0: 0x01        (Slave)
Byte 1: 0x06        (FC06: Write single)
Byte 2: 0x02
Byte 3: 0x01        (Register 0x0201)
Byte 4: 0x13        (Value high)
Byte 5: 0x88        (Value low = 0x1388 = 5000)
Byte 6: 0x??
Byte 7: 0x??        (CRC)
```

**Response (Echo of request):**
```
Same as request (typical Modbus behavior)
```

---

## 9. Configuration in iot_config.json

### Minimal Configuration (Single Hose)

```json
{
  "ro_code": "COMPAC_CNG_PUMP_001_HOSE_1",
  "c_id": 3001,
  "c_make": "Compac_CNG_Dispenser",
  "post_interval": 30,
  "status_interval": 900,
  "mbus_RTU": {
    "slave": 1,
    "baud": 9600,
    "dataBits": 8,
    "parity": "N",
    "stopBits": 1
  },
  "m": [
    256, 16
  ]
}
```

**Register range explanation:**
- Start at 0x0100 (256 decimal) = register 0100
- Read 16 registers (256–0x010F) covering:
  - 0100–0101: Status & Price
  - 0102–0103: Quantity
  - 0104–0105: Amount
  - 0106–0108: Quantity Totalizer (BCD)
  - 0109–0111: Monetary Totalizer (BCD)
  - 0112–0116: Pressure, Flowrate, Errors, Temps

---

## 10. Multi-Nozzle Support

For dual-hose Compac dispenser, create **two separate device configurations**:

**Hose 1 Config (Slave 1):**
```json
{
  "ro_code": "COMPAC_CNG_UNIT_001_HOSE_1",
  "c_id": 3001,
  "mbus_RTU": {"slave": 1, "baud": 9600, "parity": "N", ...},
  "m": [256, 16]
}
```

**Hose 2 Config (Slave 2):**
```json
{
  "ro_code": "COMPAC_CNG_UNIT_001_HOSE_2",
  "c_id": 3002,
  "mbus_RTU": {"slave": 2, "baud": 9600, "parity": "N", ...},
  "m": [256, 16]
}
```

Each hose polls independently at configured intervals (minimum 100 ms apart).

---

## 11. Polling Interval Requirements

⚠️ **Critical Constraint:**

**Minimum 100 ms between consecutive requests** (per Compac specification)

**Configuration implications:**
- Single hose: `post_interval`: 2–30 seconds (easily meets constraint)
- Dual hose (2 devices): Ensure ESP32 stagger polling by 100+ ms
- Triple+ hose: Space requests evenly across polling interval

**Example:**
```
Time 0 ms: Request Hose 1 (Slave 1)
Time 50 ms: Response from Hose 1
Time 100 ms: Request Hose 2 (Slave 2)
Time 150 ms: Response from Hose 2
Time 160 ms: All data ready, publish telemetry
```

Current system already implements timing safety (modbus_adapter mutex), so multi-hose deployment is safe.

---

## 12. Error Handling & Troubleshooting

| Issue | Likely Cause | Solution |
|-------|-------------|----------|
| **CRC errors** | Parity set to Even instead of None | Change parity from E to N in config |
| **No response / Timeout** | Slave address mismatch | Verify physical switch setting in dispenser |
| **Garbage data** | Baud rate incorrect | Confirm 9600 bps (fixed in Compac) |
| **Totalizers always 0** | BCD12 parsing not implemented | Use protocol handler or custom BCD parser |
| **Pressure/Flow always 0** | Dispenser offline | Check physical connection, dispenser power |
| **Bank bits never change** | Normal if hose idle | Bank activation depends on flow/pressure |

---

## 13. Comparison: Compac vs Gasorex

| Aspect | Gasorex | Compac |
|--------|---------|--------|
| **Parity** | Even (E) | None (N) |
| **Baud** | Configurable (9600 typical) | Fixed 9600 |
| **Nozzles** | 2 sides per unit | 1 per slave (multi-nozzle = multi-slave) |
| **Float Type** | IEEE-754 (32-bit) | BCD12 totalizers |
| **Register Range** | 1–34 | 0100–0205 (0x0100–0x0205) |
| **Register Spacing** | Consecutive | 0x0100 offset for all |
| **Polling Interval** | Flexible | Min 100 ms |
| **Status Bits** | 1 register | 2 (one status, one errors) |
| **Temperature** | Decimal | Signed integer, decimal |
| **Pressure** | Float | U16, 0.01 scale |

---

## 14. Bank Sequencing Overview

Compac uses pressure/flow-based bank switching to optimize delivery speed:

**Low Bank (Bank 1):**
- Lowest pressure, high flow
- Used for initial rapid fill
- Activated when flow > threshold (register 0204)

**Medium Bank (Bank 2):**
- Medium pressure, medium flow
- Used for transition fill
- Activated when flow drops below threshold

**High Bank (Bank 3):**
- Highest pressure, low flow
- Used for final pressure finalization
- Activated when minimum flow condition met (register 0205)

The values in registers 0202 (target) and 0203 (maximum) control when transitions occur.

---

## 15. References

- Modbus RTU Standard: IEC 61158-2
- RS-485 Standard: EIA/TIA-485-A
- BCD Encoding: ISO 6093
- CNG Dispenser Regulations: Refer to local fuel quality standards
- Compac Documentation: GSR-CU-23 controller manual (specific to your deployment)

---

## Appendix: Quick Register Reference

| Reg | Name | Type | Scale | RO/RW |
|-----|------|------|-------|-------|
| 0100 | Status Bits | U16 | - | RO |
| 0101 | Unit Price | U16 | 0.01 | RO |
| 0102–03 | Qty Dispensed | U32 | 0.001 | RO |
| 0104–05 | Amount Dispensed | U32 | 0.01 | RO |
| 0106–08 | Qty Totalizer | BCD12 | 0.001 | RO |
| 0109–11 | Amt Totalizer | BCD12 | 0.01 | RO |
| 0112 | Pressure | U16 | 0.01 | RO |
| 0113 | Flowrate | U16 | 0.01 | RO |
| 0114 | Error Bits | U16 | - | RO |
| 0115 | Ambient Temp | INT16 | 0.1 | RO |
| 0116 | Gas Temp | INT16 | 0.1 | RO |
| 0200 | Lock Control | U16 | - | RW |
| 0201 | Unit Price Set | U16 | 0.01 | RW |
| 0202 | Target Pressure | U16 | 0.01 | RW |
| 0203 | Max Pressure | U16 | 0.01 | RW |
| 0204 | Flow % Threshold | U16 | - | RW |
| 0205 | Min Flow Rate | U16 | 0.01 | RW |
