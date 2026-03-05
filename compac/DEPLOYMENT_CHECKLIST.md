# Compac CNG Dispenser - Production Deployment Checklist

## Pre-Deployment Phase (Weeks 1-2)

### Hardware Readiness Assessment

- [ ] **Physical Connection Verification**
  - [ ] RS-485 cable installed between ESP32 and dispenser
  - [ ] Cable terminated with 120Ω resistors at both ends
  - [ ] A/B wire pair identified and tested for continuity
  - [ ] No short circuits or intermittent connections detected

- [ ] **Dispenser Configuration**
  - [ ] Physical side switch set for correct slave address (01-99)
  - [ ] Document actual slave address used
  - [ ] Manual Modbus test with analyzer: inject FC03 request to 0x0100
  - [ ] Verify dispenser responds with status bits
  - [ ] Confirm baud rate: 9600 baud
  - [ ] Confirm parity: **None (N)** - NOT Even
  - [ ] Confirm stop bits: 1

- [ ] **Serial Analyzer Test (Optional but Recommended)**
  - [ ] Use commercial Modbus RTU analyzer (e.g., Advanced Serial Data Logger)
  - [ ] Capture 3-5 live request-response cycles
  - [ ] Verify byte patterns match specification
  - [ ] Confirm CRC values match calculated values
  - [ ] Document sample frames for reference

---

## Firmware Preparation Phase

### ESP32 Firmware Build

- [ ] **Dependency Verification**
  - [ ] `modbus_adapter` component available
  - [ ] `iot_hub` component available
  - [ ] `device_config` component available
  - [ ] No missing CMakeLists.txt files

- [ ] **Code Compilation**
  - [ ] Pull Compac protocol handler from `lib/dispenser_protocols/compac_cng/`
  - [ ] Include header: `#include "compac_protocol_handler.h"`
  - [ ] Compile clean: `idf.py build` produces no errors
  - [ ] Binary size acceptable (< available flash)

- [ ] **Protocol Verification**
  - [ ] Test `compac_crc16_modbus()` with known vectors
    - Input: [0x01, 0x03, 0x01, 0x00, 0x00, 0x01]
    - Expected CRC: 0x84, 0x0A (verify against Modbus standard)
  - [ ] Test `compac_parse_uint16_be()` with [0xAB, 0xCD]
    - Expected result: 0xABCD = 43981
  - [ ] Test `compac_parse_bcd12()` with [0x12, 0x34, 0x56, 0x78, 0x90, 0x12]
    - Expected string: "123456789012"

---

## Configuration Phase

### JSON Configuration Generation

- [ ] **Single-Hose Configuration**
  - [ ] Copy `sample_config_single_hose.json` as template
  - [ ] Update `ro_code` to match your naming convention
  - [ ] Update `c_id` to unique device ID
  - [ ] Update `mbus_RTU.parity` to **"N"** (no parity)
  - [ ] Confirm `m: [256, 16]` (start address 0x0100, read 16 registers)

- [ ] **Multi-Hose Configuration (if applicable)**
  - [ ] Create separate device entry for each hose
  - [ ] Unique slave address per hose (e.g., 1, 2, 3...)
  - [ ] Unique `c_id` per configuration
  - [ ] Same register range: `[256, 16]`
  - [ ] Post intervals staggered by ≥100ms

- [ ] **Azure IoT Hub Device Twin Setup**
  - [ ] Register device in IoT Hub
  - [ ] Store connection string securely
  - [ ] Verify Device Twin desired properties update capability
  - [ ] Test property callback triggers (price update, lock control)

---

## First Data Collection Phase

### Initial Polling Verification

- [ ] **Serial Monitor Observation**
  - [ ] Connect to ESP32 serial console (115200 baud)
  - [ ] Power device, start polling
  - [ ] Observe raw Modbus frames: `[Slave] [FC03] [Addr] [Data...] [CRC]`
  - [ ] Confirm no timeout errors for ≥10 consecutive reads
  - [ ] Note typical response time (should be <50ms)

- [ ] **Register Data Validation**
  - [ ] Status register (0x0100) changes with dispenser state
  - [ ] Unit price register (0x0101) reads non-zero
  - [ ] Quantity (0x0102-03) increments during delivery
  - [ ] Totalizers (0x0106-08, 0x0109-0B) match previous session values
  - [ ] Pressure/Flowrate (0x0112-13) values in expected range (0-655.35)
  - [ ] Temperature registers show realistic values (-40 to +60°C typical)

- [ ] **BCD12 Parsing Validation (Critical)**
  - [ ] Enable serial logging for BCD strings: `compac_totalizer.quantity_bcd_string`
  - [ ] Confirm output is 12 digits of 0-9 (no invalid nibbles)
  - [ ] Verify `bcd_parse_valid` flag is true
  - [ ] Calculate numeric value: `NNNNNNNNNNNN × 0.001 = kg value`
  - [ ] Confirm value is reasonable (0 to 999999999.999)

- [ ] **Azure IoT Hub Telemetry Arrival**
  - [ ] Check IoT Hub metrics: "Telemetry messages received"
  - [ ] Should see one message per `post_interval` seconds
  - [ ] Confirm no authentication errors
  - [ ] View device-to-cloud messages in Azure Explorer

- [ ] **Parsing Validation (Cloud Side)**
  - [ ] Deploy one of the cloud parsers (JavaScript, Python, or C#)
  - [ ] Feed sample telemetry JSON
  - [ ] Verify BCD12 string converts to correct numeric value
  - [ ] Confirm all scaling factors applied correctly

---

## Configuration Tuning Phase

### Production Parameter Optimization

- [ ] **Polling Interval Selection**
  - [ ] For real-time dispatch: `post_interval: 5` (every 5 seconds)
  - [ ] For typical stations: `post_interval: 30` (every 30 seconds)
  - [ ] For archival: `post_interval: 60` or higher
  - [ ] Ensure all multi-hose reads complete within interval (min 100ms gaps)

- [ ] **Status Polling Interval**
  - [ ] Typical value: `status_interval: 900` (15 minutes for heartbeat)
  - [ ] Ensures device is alive even with no active transactions
  - [ ] Can increase to 3600 (1 hour) for low-activity stations

- [ ] **Modbus Parameter Validation**
  - [ ] Baud rate: confirm **9600** (Compac fixed)
  - [ ] Parity: confirm **"N"** (no parity, critical difference from Gasorex)
  - [ ] Data bits: confirm **8**
  - [ ] Stop bits: confirm **1**
  - [ ] Document actual physical switch setting on dispenser

- [ ] **Device Twin Desired Properties Setup**
  - [ ] Define update mechanism for unit price (register 0x0201)
  - [ ] Define lock/unlock control (register 0x0200)
  - [ ] Define target pressure (register 0x0202)
  - [ ] Test Device Twin write triggers protocol frame construction

---

## Monitoring & Alerting Setup

### Real-Time Anomaly Detection

- [ ] **Azure Stream Analytics Configuration**
  - [ ] Deploy SQL query for pressure > max_fill_pressure
  - [ ] Alert: `data.pressure.dispenser_pressure_bar > 300`
  - [ ] Alert: `data.errors.reverse_flow_detected = true`
  - [ ] Alert: `data.errors.mfm_fault = true`
  - [ ] Alert: `data.totalizer.bcd_parse_valid = false` (firmware issue)
  - [ ] Output alerts to Event Hub for escalation

- [ ] **Application Insights Monitoring**
  - [ ] Track cloud parser success rate (target: >99.5%)
  - [ ] Monitor BCD parse failures per device
  - [ ] Alert if single device has >5 parse failures/hour
  - [ ] Track CRC error rate from ESP32 logs

- [ ] **Power BI Dashboard Creation**
  - [ ] Real-time pressure vs. target pressure chart
  - [ ] Cumulative delivery quantity by station
  - [ ] Unit price history (track changes)
  - [ ] Error flags heatmap by device
  - [ ] Daily/weekly/monthly sales reports

---

## Production Validation (Go/No-Go)

### Final System-Level Testing (Day 1 in Production)

- [ ] **Functional Requirements**
  - [ ] Device reads and streams telemetry ✓
  - [ ] BCD12 totalizers parse correctly ✓
  - [ ] Unit price updates via Device Twin ✓
  - [ ] Dispenser lock control works ✓
  - [ ] Pressure readings match dispenser display ±1% ✓

- [ ] **Non-Functional Requirements**
  - [ ] Polling latency: <2 seconds (request to cloud) ✓
  - [ ] Message loss rate: <0.1% ✓
  - [ ] Parse success rate: >99.5% ✓
  - [ ] CRC error rate: <0.5% ✓

- [ ] **Stress Test (1 hour continuous operation)**
  - [ ] No memory leaks in ESP32
  - [ ] No watchdog resets
  - [ ] All 1000+ messages parsed successfully
  - [ ] Totalizers increment correctly over time

---

## Maintenance & Operations Phase (Ongoing)

### Daily Operations Checklist

- [ ] **Morning Data Validation**
  - [ ] Confirm telemetry arriving in IoT Hub
  - [ ] Spot-check pressure values against physical display
  - [ ] Review error flags: any alerts overnight?
  - [ ] Confirm totalizers incrementing correctly

- [ ] **Weekly Verification**
  - [ ] Manual dispenser test: inject FC03 request with analyzer
  - [ ] Compare analyzer readings to ESP32 parsed values
  - [ ] Verify parity setting still "N" (no parity)
  - [ ] Check RS-485 cable for damage/corrosion

- [ ] **Monthly Review**
  - [ ] Analyze BCD parse failure rate (target: zero)
  - [ ] Review CRC error trends (should be <0.1%)
  - [ ] Validate totalizer accuracy: physical count vs. electronic
  - [ ] Document any configuration changes

---

## Troubleshooting Decision Tree

### Issue: "No Response from Dispenser"

```
1. Power to dispenser?
   ├─→ NO: Turn on dispenser
   └─→ YES: Continue

2. RS-485 physical connection?
   ├─→ Disconnected/broken: Repair cable
   └─→ Connected: Continue

3. CRC errors in ESP32 logs?
   ├─→ YES: Check parity setting (must be "N", not "E")
   └─→ NO: Continue

4. Baud rate correct (9600)?
   ├─→ NO: Update config
   └─→ YES: Continue

5. Slave address correct?
   ├─→ NO: Check physical switch inside dispenser, update config
   └─→ YES: Continue

ACTION: Use Modbus RTU analyzer to directly poll dispenser
```

### Issue: "BCD12 Parse Failed"

```
1. ESP32 logs show non-decimal nibbles?
   ├─→ YES: RS-485 frame corruption
   │         → Check cable termination
   │         → Check for noise/EMI
   └─→ NO: Continue

2. bcd_parse_valid = false?
   ├─→ YES: Firmware bug in compac_parse_bcd12()
   │         → Recompile, verify CRC test
   │         → Check if totalizer register actually responds
   └─→ NO: Continue

3. Numeric value unreasonable (>999999999)?
   ├─→ YES: Byte-order error
   │         → Verify compac_parse_bcd12() uses big-endian
   │         → Check register mapping
   └─→ NO: Continue

ACTION: Capture raw Modbus frame, manually decode bytes
```

### Issue: "Pressure Reading Incorrect"

```
1. Dispenser display vs. ESP32 differ by >±5%?
   ├─→ YES: Continue
   └─→ NO: Normal, variance acceptable

2. Scale factor wrong?
   ├─→ Check: pressure_raw × 0.01 = displayed value
   │   Dispenser shows 250.5 bar
   │   Register value should be 25050 decimal
   │   Bytes: [0x61, 0xBA] = 0x61BA = 25018 ≈ 250.18 bar
   └─→ Adjust if formula differs

3. Register mapping off by 2 bytes?
   ├─→ Verify offset: 0x0112 = data[24-25] in frame
   │   (0x0100=0, 0x0101=2, 0x0102=4... 0x0112=24)
   └─→ Correct offset in parsing code

ACTION: Compare 3 readings: dispenser display, analyzer, ESP32
```

---

## Rollback Procedure (If Critical Issue Found)

**Situation:** Device malfunction detected in production > 5% of messages affected

1. Stop polling immediately: Disable device in IoT Hub
2. Revert firmware to last known-good version
3. Diagnose offline using Modbus analyzer
4. Fix and recompile
5. Test in lab bench before re-enabling
6. Incremental rollout: 1 device, then 10%, then 100%

**Estimated rollback time:** 30 minutes to re-enable

---

## Success Criteria (Sign-Off)

Project sign-off requires:

- [x] ≥100 consecutive error-free Modbus transactions
- [x] BCD12 parse success rate = 100%
- [x] CRC error rate < 0.5% over 24-hour window
- [x] Pressure readings accurate within ±2%
- [x] Totalizers match manual count within 0.1%
- [x] Device Twin price/lock updates work
- [x] Cloud parsing successful for all telemetry
- [x] No data loss during 1-hour continuous operation
- [x] Documentation complete and reviewed
- [x] Operations team trained on troubleshooting

---

## References

- Compac CNG Dispenser Manual: Refer to manufacturer documentation
- Protocol Specification: `PROTOCOL.md` in this package
- Register Map: `register_map.csv` in this package
- Cloud Parsing: `CLOUD_IMPLEMENTATION.md` in this package
- Modbus RTU Standard: IEC 61158-2

