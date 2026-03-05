# Gasorex SR Energy Dispenser Protocol Specification

## 1. Overview

**Manufacturer:** Gasorex  
**Model:** SR Energy Dispensing System  
**Controller Unit:** GSR-CU-23  
**Display Module:** GSR-SD-23  
**Communication Protocol:** Modbus RTU (RS-485 serial)  
**Application:** Gas/LPG dispensing with dual-side (Side A & Side B) capability  
**Power:** 230 V AC single phase → 24 V DC (2.5 A) via SMPS  

---

## 2. Physical Architecture

```
┌─────────────────────────────────────────────────────────┐
│           Gasorex SR Energy Dispenser Unit              │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  ┌────────────────────────────────────────────────┐    │
│  │         GSR-CU-23 Controller Unit              │    │
│  │                                                 │    │
│  │  ┌─────────────┬──────────────────┐            │    │
│  │  │ RS-485 Port │  (A/B diff lines) │            │    │
│  │  └─────────────┴──────────────────┘            │    │
│  └────────────────────────────────────────────────┘    │
│                      ↓                                  │
│         ┌────────────┴────────────┐                    │
│         ↓                          ↓                    │
│    ┌─────────────┐          ┌─────────────┐           │
│    │   SIDE A    │          │   SIDE B    │           │
│    ├─────────────┤          ├─────────────┤           │
│    │ Flowmeter   │          │ Flowmeter   │           │
│    │ PT1, PT2    │          │ PT3, PT4    │           │
│    │ Valve Bank: │          │ Valve Bank: │           │
│    │ DB1,LB1,    │          │ DB2,LB2,    │           │
│    │ MB1,HB1     │          │ MB2,HB2     │           │
│    └─────────────┘          └─────────────┘           │
│                                                         │
│  ┌────────────────────────────────────────────────┐    │
│  │      GSR-SD-23 Display Module                   │    │
│  │      LCD + Keypad Interface                     │    │
│  └────────────────────────────────────────────────┘    │
│                                                         │
└─────────────────────────────────────────────────────────┘
                           ↑
                    SCADA Integration
                   (RS-485 Monitoring)
```

---

## 3. Electrical Specifications

| Parameter | Specification |
|-----------|---------------|
| **Input Voltage** | 230 V AC, single phase |
| **Power Supply** | SMPS (Switched-Mode Power Supply) |
| **Output Voltage** | 24 V DC ±3% |
| **Output Current** | 2.5 A maximum |
| **Pressure Sensors (PT1–PT4)** | 0–400 bar range |
| **Analog Output** | 0.5–4.5 V (4–20 mA equivalent) |
| **Gas Flow Lines** | Two independent sides (A and B) |

---

## 4. RS-485 Communication Specification

### 4.1 Physical Layer

| Parameter | Value |
|-----------|-------|
| **Standard** | RS-485 (EIA/TIA-485) |
| **Line Configuration** | Differential (A/B pair) |
| **Transmission Mode** | Half-duplex |
| **Max Cable Length** | 1200 m (typical industrial) |
| **Termination** | 120 Ω resistors at line ends |

### 4.2 Serial Parameters

| Parameter | Default Value | Notes |
|-----------|---------------|-------|
| **Baud Rate** | 9600 bps | Configurable, see controller manual |
| **Data Bits** | 8 | Fixed |
| **Stop Bits** | 1 | Standard for Modbus RTU |
| **Parity** | Even (E) | May vary; check SCADA register map |
| **Error Control** | CRC-16 (Modbus RTU) | 16-bit cyclic redundancy check |

### 4.3 Slave Addressing

Each dispenser unit or nozzle is accessed via a **configurable slave address** (typically **1–247**).

**Recommended Configuration:**
- **Slave Address 1:** GSR-CU-23 Main Controller
- **Slave Address 2–4:** Additional units (if daisy-chained)

---

## 5. Modbus RTU Frame Structure

### 5.1 Request Frame

```
[Slave ID] [Function Code] [Register Address] [Register Count] [CRC-Lo] [CRC-Hi]
   1 byte       1 byte           2 bytes           2 bytes      1 byte   1 byte
```

### 5.2 Response Frame

```
[Slave ID] [Function Code] [Byte Count] [Register Data...] [CRC-Lo] [CRC-Hi]
   1 byte       1 byte         1 byte        N bytes         1 byte   1 byte
```

### 5.3 Function Codes Used

| FC | Name | Purpose | Read/Write |
|----|------|---------|-----------|
| **03** | Read Holding Registers | Read configuration, settings | Read-Only |
| **04** | Read Input Registers | Read sensor data, measurements | Read-Only |
| **06** | Write Single Register | Control emergency stop, set parameters | Write-Only |
| **16** | Write Multiple Registers | Write multiple configuration registers | Write-Only |

---

## 6. SCADA Register Map

### 6.1 Side A (Amount, Quantity, Totalizers, Pressure)

| Register | FC | Type | Name | Unit | Scale | Function | Access |
|----------|----|----|------|------|-------|----------|--------|
| **01** | 4 | U16 (High) | Side A Amount | Rs | 0.01 | Dispensed amount in this transaction | RO |
| **02** | 4 | U16 (Low) | Side A Amount | Rs | 0.01 | (Float: 32-bit across 01–02) | RO |
| **03** | 4 | U16 (High) | Side A Quantity | Kg/L | 0.01 | Dispensed quantity in transaction | RO |
| **04** | 4 | U16 (Low) | Side A Quantity | Kg/L | 0.01 | (Float: 32-bit across 03–04) | RO |
| **05** | 4 | U16 (High) | Side A Electronic Totalizer | Rs | 0.01 | Cumulative amount (electronic) | RO |
| **06** | 4 | U16 (Low) | Side A Electronic Totalizer | Rs | 0.01 | (Float: 32-bit across 05–06) | RO |
| **07** | 4 | U16 (High) | Side A Flowmeter Totalizer | Kg/L | 0.01 | Cumulative quantity (flowmeter) | RO |
| **08** | 4 | U16 (Low) | Side A Flowmeter Totalizer | Kg/L | 0.01 | (Float: 32-bit across 07–08) | RO |
| **09** | 4 | U16 | Side A PT1 Pressure | bar | 0.1 | Pressure transmitter 1 | RO |
| **10** | 4 | U16 | Side A PT2 Pressure | bar | 0.1 | Pressure transmitter 2 | RO |
| **11** | 4 | U16 (High) | Side A Flowrate | kg/min | 0.001 | Current flowrate | RO |
| **12** | 4 | U16 (Low) | Side A Flowrate | kg/min | 0.001 | (Float: 32-bit across 11–12) | RO |
| **13** | 4 | U16 | Side A Error Status | - | - | Bit flags (see table below) | RO |
| **14** | 3 | U16 | Side A Emergency Stop | - | - | 1=set stop, 0=reset (toggle) | R/W |
| **15** | 4 | U16 | Side A Temperature | °C | 0.1 | Optional: pump/solenoid temperature | RO |

**Side A Error Status Flags (Register 13):**
```
Bit 0: Flowmeter error / disconnected
Bit 1: Pressure transmitter error
Bit 2: Solenoid valve fault
Bit 3: Safety interlock triggered
Bit 4: Over-temperature warning
Bit 5: Under-pressure warning
Bit 6: Over-pressure warning
Bit 7: Control module fault
Bits 8–15: Reserved
```

---

### 6.2 Side B (Same Structure as Side A)

| Register | FC | Type | Name | Unit | Scale | Function | Access |
|----------|----|----|------|------|-------|----------|--------|
| **16** | 4 | U16 (High) | Side B Amount | Rs | 0.01 | (Float: 32-bit across 16–17) | RO |
| **17** | 4 | U16 (Low) | Side B Amount | Rs | 0.01 | | RO |
| **18** | 4 | U16 (High) | Side B Quantity | Kg/L | 0.01 | (Float: 32-bit across 18–19) | RO |
| **19** | 4 | U16 (Low) | Side B Quantity | Kg/L | 0.01 | | RO |
| **20** | 4 | U16 (High) | Side B Electronic Totalizer | Rs | 0.01 | (Float: 32-bit across 20–21) | RO |
| **21** | 4 | U16 (Low) | Side B Electronic Totalizer | Rs | 0.01 | | RO |
| **22** | 4 | U16 (High) | Side B Flowmeter Totalizer | Kg/L | 0.01 | (Float: 32-bit across 22–23) | RO |
| **23** | 4 | U16 (Low) | Side B Flowmeter Totalizer | Kg/L | 0.01 | | RO |
| **24** | 4 | U16 | Side B PT1 Pressure | bar | 0.1 | Pressure transmitter 3 | RO |
| **25** | 4 | U16 | Side B PT2 Pressure | bar | 0.1 | Pressure transmitter 4 | RO |
| **26** | 4 | U16 (High) | Side B Flowrate | kg/min | 0.001 | (Float: 32-bit across 26–27) | RO |
| **27** | 4 | U16 (Low) | Side B Flowrate | kg/min | 0.001 | | RO |
| **28** | 4 | U16 | Side B Error Status | - | - | Bit flags (same as Side A) | RO |
| **29** | 3 | U16 | Side B Emergency Stop | - | - | 1=set stop, 0=reset (toggle) | R/W |
| **30** | 4 | U16 | Side B Temperature | °C | 0.1 | Optional: pump/solenoid temperature | RO |

---

### 6.3 Configuration Registers (Shared)

| Register | FC | Type | Name | Unit | Scale | Function | Access |
|----------|----|----|------|------|-------|----------|--------|
| **31** | 3 | U16 (High) | First Rate (Pricing) | Rs/kg | 0.01 | Rate 1 cost per unit (Float across 31–32) | R/W |
| **32** | 3 | U16 (Low) | First Rate (Pricing) | Rs/kg | 0.01 | | R/W |
| **33** | 3 | U16 (High) | Second Rate (Pricing) | Rs/kg | 0.01 | Rate 2 cost per unit (Float across 33–34) | R/W |
| **34** | 3 | U16 (Low) | Second Rate (Pricing) | Rs/kg | 0.01 | | R/W |

---

## 7. Data Type Handling

### 7.1 32-bit Floats (Split Across 2×16-bit Registers)

**Big-Endian Format (Most Significant Word First):**

```
Example: Side A Amount at registers 01–02
  Reg 01 (High 16 bits) = 0x4201  // MSW
  Reg 02 (Low 16 bits)  = 0x0000  // LSW
  
  Combined 32-bit value: 0x42010000
  As IEEE-754 float: ≈ 32.25 Rs
```

**Parsing in C:**
```c
uint16_t reg_high = buffer[0] << 8 | buffer[1];  // Register 01
uint16_t reg_low = buffer[2] << 8 | buffer[3];   // Register 02

uint32_t combined = (reg_high << 16) | reg_low;
float value = *(float*)&combined;  // IEEE-754 reinterpretation
```

### 7.2 16-bit Integers (Unsigned)

Simple 16-bit unsigned integer, big-endian:
```c
uint16_t value = (buffer[0] << 8) | buffer[1];
```

### 7.3 Signed 16-bit Integers (Temperature)

Two's complement:
```c
int16_t value = (int16_t)((buffer[0] << 8) | buffer[1]);
```

---

## 8. CRC-16 Calculation (Modbus RTU)

**Polynomial:** 0xA001 (reflected)

**C Implementation:**
```c
uint16_t crc16_modbus(const uint8_t *data, size_t len) {
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

## 9. Communication Example (Side A Flowrate Read)

### Request (Read Input Registers 11–12 for Side A Flowrate)

```
Byte 0: 0x01        (Slave ID = 1)
Byte 1: 0x04        (Function Code = Read Input Registers)
Byte 2: 0x00        (Register Address High)
Byte 3: 0x0B        (Register Address Low = 11 decimal)
Byte 4: 0x00        (Quantity High)
Byte 5: 0x02        (Quantity Low = 2 registers)
Byte 6: 0xE4        (CRC-16 Low)
Byte 7: 0x07        (CRC-16 High)

Total: 8 bytes
```

### Response (Flowrate = 15.5 kg/min)

```
Byte 0: 0x01        (Slave ID = 1)
Byte 1: 0x04        (Function Code = Read Input Registers)
Byte 2: 0x04        (Byte count = 4)
Byte 3: 0x42        (Reg 11 High = 0x42)
Byte 4: 0x7A        (Reg 11 Low = 0x7A)
Byte 5: 0x00        (Reg 12 High = 0x00)
Byte 6: 0x00        (Reg 12 Low = 0x00)
Byte 7: 0x3A        (CRC-16 Low)
Byte 8: 0x21        (CRC-16 High)

Decoded: 0x427A0000 = 15.5 as IEEE-754 float
```

---

## 10. Emergency Stop Control (Registers 14 & 29)

### Writing to Emergency Stop

**Function Code:** 06 (Write Single Register)

**Example: Set Side A Emergency Stop**
```
Byte 0: 0x01        (Slave ID = 1)
Byte 1: 0x06        (Function Code = Write Single Register)
Byte 2: 0x00        (Register Address High)
Byte 3: 0x0E        (Register Address Low = 14 decimal)
Byte 4: 0x00        (Value High)
Byte 5: 0x01        (Value Low = 1, set stop)
Byte 6: 0xLA        (CRC-16 Low)
Byte 7: 0x20        (CRC-16 High)
```

**Reset Emergency Stop (Value = 0):**
```
...
Byte 5: 0x00        (Value Low = 0, reset stop)
...
```

---

## 11. Configuration in iot_config.json

### Minimal Configuration (Read Both Sides)

```json
{
  "ro_code": "GASOREX_SR_ENERGY_UNIT_001",
  "c_id": 2001,
  "c_make": "Gasorex_SR_Energy",
  "post_interval": 30,
  "status_interval": 900,
  "mbus_RTU": {
    "slave": 1,
    "baud": 9600,
    "dataBits": 8,
    "parity": "E",
    "stopBits": 1
  },
  "m": [
    1, 14,
    16, 15
  ]
}
```

**Register ranges:**
- `[1, 14]` → Registers 1–14 (Side A data: amount, quantity, totalizers, pressure, flowrate, error, e-stop)
- `[16, 15]` → Registers 16–30 (Side B data: same structure as Side A)

**Total data:** 29 registers × 2 bytes = 58 bytes per poll

---

## 12. Multi-Nozzle Support

The Gasorex SR Energy inherently supports **2 sides (dispensing lines)** within a single unit. However, if you have multiple independent units or nozzles:

### Option A: Single Unit with Both Sides
Use one device config with both register ranges (recommended for simplicity).

### Option B: Separate Devices for Each Side
If you need independent control or monitoring:

**Device 1 (Side A only):**
```json
{
  "ro_code": "GASOREX_SR_ENERGY_UNIT_001_SIDE_A",
  "c_id": 2001,
  "mbus_RTU": {"slave": 1, ...},
  "m": [1, 14]
}
```

**Device 2 (Side B only):**
```json
{
  "ro_code": "GASOREX_SR_ENERGY_UNIT_001_SIDE_B",
  "c_id": 2002,
  "mbus_RTU": {"slave": 1, ...},
  "m": [16, 15]
}
```

### Option C: Multiple Physical Units (Daisy-Chained)
If you have multiple Gasorex units on the same RS-485 network:

```json
{
  "ro_code": "GASOREX_NETWORK",
  "c_id": 2000,
  "mbus_RTU": {"slave": 1, ...},
  "m": [
    1, 14,      // Unit 1, Side A
    16, 15,     // Unit 1, Side B
    1, 14,      // Unit 2, Side A (would require separate call with slave=2)
    16, 15      // Unit 2, Side B
  ]
}
```

**Note:** Current implementation reads from a single slave address. For multiple units, create separate device configs with different slave addresses.

---

## 13. Troubleshooting

| Issue | Cause | Solution |
|-------|-------|----------|
| **CRC Errors** | Parity mismatch or baud rate incorrect | Verify baud=9600, parity=Even, 8 data bits, 1 stop |
| **No Response** | Slave address wrong or bus disconnected | Check slave address (default 1), verify RS-485 termination |
| **Garbage Data** | Big-endian/little-endian confusion | Verify byte order matches Modbus RTU standard (big-endian) |
| **Timeout** | Device offline or slow responding | Increase timeout to 500 ms, check physical RS-485 connection |
| **Float Parsing Error** | Incorrect register combination | Ensure using big-endian 32-bit combination: (Reg_High << 16) \| Reg_Low |

---

## 14. References

- **Modbus RTU Standard:** IEC 61158-2
- **RS-485 Standard:** EIA/TIA-485-A
- **IEEE-754:** Single-precision floating-point format
- **Gasorex Documentation:** Refer to GSR-CU-23 controller manual for detailed register specifications and timing

---

## Appendix: Quick Register Reference

| Parameter | Side A Regs | Side B Regs | Type | Scale |
|-----------|------------|------------|------|-------|
| Amount (Rs) | 1–2 | 16–17 | Float | 0.01 |
| Quantity (Kg) | 3–4 | 18–19 | Float | 0.01 |
| E-Totalizer (Rs) | 5–6 | 20–21 | Float | 0.01 |
| FM-Totalizer (Kg) | 7–8 | 22–23 | Float | 0.01 |
| PT1 Pressure (bar) | 9 | 24 | U16 | 0.1 |
| PT2 Pressure (bar) | 10 | 25 | U16 | 0.1 |
| Flowrate (kg/min) | 11–12 | 26–27 | Float | 0.001 |
| Error Status | 13 | 28 | U16 | - |
| E-Stop Control | 14 | 29 | U16 | - |
| Temperature (°C) | 15 | 30 | I16 | 0.1 |
| First Rate (Rs/kg) | 31–32 | - | Float | 0.01 |
| Second Rate (Rs/kg) | 33–34 | - | Float | 0.01 |
