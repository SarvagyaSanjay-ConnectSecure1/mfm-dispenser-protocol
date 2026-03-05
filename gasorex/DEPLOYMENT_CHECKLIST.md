# Gasorex SR Energy Deployment Checklist

## Pre-Deployment Assessment

### Hardware Connection
- [ ] Wired RS-485 differential pair (A and B lines) from ESP32 to GSR-CU-23 controller
- [ ] RS-485 termination resistors (120Ω) installed at line ends
- [ ] 24V DC power supply connected to GSR-CU-23
- [ ] Tested with multimeter: differential voltage between A and B (idle ~0V, active ±2-5V)
- [ ] Checked shell grounding between ESP32 and GSR-CU-23

### Gasorex Controller Configuration
- [ ] GSR-CU-23 powered on and LED indicators normal
- [ ] RS-485 communication parameters confirmed:
  - [ ] Baud rate: 9600 bps (or documented alternate)
  - [ ] Parity: Even (E) - **verify with Gasorex manual**
  - [ ] Data bits: 8
  - [ ] Stop bits: 1
- [ ] Slave address set (default 1, confirm in GSR-CU-23 configuration menu)
- [ ] Both Side A and Side B physically connected (flowmeters, pressure sensors, solenoid valves)
- [ ] Display module (GSR-SD-23) responding normally
- [ ] Emergency stop mechanism tested manually (if available)

### Preliminary Software Test

Before deploying to production, test with a Modbus RTU analyzer tool:

- [ ] Installed ModbusRTU test tool (e.g., MultiMaster, QModMaster, or Modbus RTU Master)
- [ ] Manually read register 1 (Side A Amount) - should return a valid value
- [ ] Manually read register 16 (Side B Amount) - should return a valid value
- [ ] Manually read register 13 (Side A Error Status) - should be 0x0000 if no errors
- [ ] Manually read register 14 (Side A Emergency Stop) - should be 0x0000 (not active)
- [ ] CRC validation passed on all test reads

**If manual test fails:** Do not proceed to ESP32 deployment. Troubleshoot RS-485 physical connection and Gasorex configuration first.

---

## ESP32 Firmware Deployment

### Prepare Configuration File

1. **Choose configuration template:**
   - For reading both sides from single unit: Use `sample_config_single_unit.json`
   - For treating sides separately: Use `sample_config_separate_sides.json` + `sample_config_side_b.json`

2. **Customize the JSON:**
   ```json
   {
     "ro_code": "SITE_001_PUMP_A",  // Unique identifier for this pump unit
     "c_id": 2001,                   // Globally unique device ID
     "c_make": "Gasorex_SR_Energy_GSR_CU_23",
     "post_interval": 30,             // Adjust based on monitoring frequency needs
     "mbus_RTU": {
       "slave": 1,                   // **CONFIRM THIS MATCHES GSR-CU-23 SETTING**
       "baud": 9600,                 // **CONFIRM THIS MATCHES YOUR SYSTEM**
       "dataBits": 8,
       "parity": "E",                // Even parity (standard for Gasorex)
       "stopBits": 1
     },
     "m": [1, 14, 16, 15]           // Read Side A (1-14) and Side B (16-30)
   }
   ```

3. **Validate JSON syntax:**
   ```bash
   # On Windows PowerShell
   $json = Get-Content iot_config.json | ConvertFrom-Json
   # If no error, syntax is valid
   ```

### Load Configuration to Device

**Option A: SD Card (Boot-Time Load)**
- [ ] Copy `iot_config.json` to SD card root directory
- [ ] Insert SD card into ESP32
- [ ] Power cycle ESP32
- [ ] Check serial logs: Should see `Config parsed: RTU slave=1 regs=29...`

**Option B: Azure Device Twin (Runtime Push)**
- [ ] Create device in Azure IoT Hub
- [ ] Set Device Twin desired properties:
  ```json
  {
    "properties": {
      "desired": {
        "ro_code": "SITE_001_PUMP_A",
        "mbus_RTU": {
          "slave": 1,
          "baud": 9600,
          "dataBits": 8,
          "parity": "E",
          "stopBits": 1
        },
        "m": [1, 14, 16, 15]
      }
    }
  }
  ```
- [ ] Device receives and applies config automatically

### Monitor ESP32 Startup Logs

Connect to ESP32 serial console (115200 baud) and observe:

```
[main] Board: DIGITALPETRO_V1  Chip-ID: xxxx-xxxx
[main] Initializing LittleFS...
[main] SD card mounted: 1 files found
[dev_cfg] Config parsed: RTU slave=1 regs=29 interval=30s ro=SITE_001_PUMP_A c_id=2001
[modbus] RTU mode: slave=1, baud=9600, parity=E, 8 data bits, 1 stop
[wifi_mgr] Connected to SSID = "your-network" (RSSI = -42 dBm)
[time_utils] Time synced via SNTP: 2025-03-05 14:30:45
[iot_hub] MQTT connected to Azure IoT Hub
[main] Poller: 29 entries, interval=30s
```

**If you see errors:**
- `CRC error` or `timeout` → Check baud rate, parity, slave address
- `cannot create socket` → Check WiFi connection, MQTT config
- `Config not loaded` → Ensure `iot_config.json` is valid JSON and on SD card

---

## First Data Collection

### Verify Telemetry Reaching Azure

1. **View in Azure IoT Hub Explorer:**
   - [ ] Open Azure IoT Hub
   - [ ] Select device
   - [ ] Monitor Device-to-Cloud messages (D2C)
   - [ ] Should see new telemetry every 30 seconds (or configured interval)

2. **Sample telemetry JSON:**
   ```json
   {
     "timestamp": "2025-03-05T14:30:45Z",
     "reg_data": [
       {
         "addr": 1,
         "len": 14,
         "data": "4201000041800000414000004100000041c0000041a00000000000000000000060000000"
       },
       {
         "addr": 16,
         "len": 15,
         "data": "4200800041a0000041900000414000004060000040c0000041400000000000000600000000000"
       }
     ],
     "ro_code": "SITE_001_PUMP_A",
     "c_id": 2001,
     "connected": true
   }
   ```

### Parse and Validate Data

Using the cloud-side parser (JavaScript, Python, C#...):

```javascript
const parsed = GasorexParser.parseRegData(telemetry.reg_data);
console.log('Side A Flowrate:', parsed.side_a.flowrate_kg_per_min, 'kg/min');
console.log('Side B Amount:', parsed.side_b.amount_rs, 'Rs');
console.log('Errors:', parsed.side_a.error_status_flags);
```

**Validation checks:**
- [ ] Flowrate is reasonable (typically 0–100 kg/min for dispensers)
- [ ] Pressure values are within 0–400 bar
- [ ] Amount values increase or remain stable (shouldn't jump wildly)
- [ ] Error flags show no critical errors (flowmeter_error = false)
- [ ] Timestamp is current (not old data)

---

## Production Configuration

### Performance Tuning

| Setting | Default | Performance | Notes |
|---------|---------|-------------|-------|
| `post_interval` | 30s | 30–60s | Shorter = more responsive, more bandwidth |
| `status_interval` | 900s | 300–1800s | Less frequent for large deployments |
| Register count | 29 | Keep minimal | More registers = longer read time, larger payload |

**Recommended for continuous monitoring:** 30s interval  
**Recommended for historical logging:** 60–120s interval  

### Alarm & Alert Rules

Define in Azure Stream Analytics or Logic Apps:

```sql
-- Alert: Flowmeter failure
SELECT device_id, 'CRITICAL' as severity, 'Flowmeter Error' as alert_type
WHERE error_status_flags.flowmeter_error = true

-- Alert: Overpressure
SELECT device_id, 'WARNING' as severity, 'Overpressure: ' + pt1_pressure_bar + ' bar'
WHERE pt1_pressure_bar > 380

-- Alert: Emergency stop active
SELECT device_id, 'WARNING' as severity, 'Emergency stop active'
WHERE emergency_stop_active = true
```

### Data Retention & Archiving

- [ ] Set up automatic data export to Azure Data Lake or Blob Storage
- [ ] Retention policy: 90 days hot (real-time), 2 years cold (archive)
- [ ] Create backup of configuration files (JSON + register map)
- [ ] Document any system-specific customizations

---

## Monitoring & Maintenance

### Daily Checks

- [ ] Telemetry arriving in Azure (check IoT Hub metrics)
- [ ] No CRC errors in ESP logs (should see 0% error rate)
- [ ] Pressure readings within normal operating range
- [ ] No critical alert flags triggering

### Weekly Checks

- [ ] Review flowrate trends (any unusual patterns?)
- [ ] Check temperature (if available) for sensor health
- [ ] Verify electronic and flowmeter totalizers are incrementing
- [ ] Confirm emergency stop control responds (test in safe environment if applicable)

### Monthly Maintenance

- [ ] Export telemetry report for regulatory compliance
- [ ] Verify RS-485 cable integrity (visual inspection, continuity test)
- [ ] Cross-check electronic totalizer with manual pump counter (if available)
- [ ] Update documentation if configuration changes made

---

## Troubleshooting Decision Tree

### Symptom: No telemetry in Azure

```
├─ Check WiFi connection
│  └─ If disconnected → Move AP closer or increase signal
└─ Check MQTT config file (mqtt_config.json)
   └─ If missing → Copy to SD card or set via Azure Twin
```

### Symptom: Telemetry present but CRC errors in logs

```
├─ Verify baud rate matches GSR-CU-23
│  └─ If wrong → Update iot_config.json, reboot
├─ Check RS-485 cable (no loose connections)
│  └─ If loose → Reseat connectors
└─ Verify parity setting (should be Even for Gasorex)
   └─ If wrong → Update iot_config.json, reboot
```

### Symptom: Data values always 0.0 or NaN

```
├─ Verify Modbus addresses match PROTOCOL.md
│  └─ If wrong → Update register list in iot_config.json
├─ Check response frame format (should be little-endian for registers)
│  └─ Use Modbus analyzer to verify raw response
└─ Confirm flowmeter is physically activated (has physical flow)
   └─ If idle → Check pump/solenoid operation
```

### Symptom: Emergency stop won't reset

```
├─ Verify register 14 (Side A) or 29 (Side B) is writable (FC03)
│  └─ If FC04 only → Configure GSR-CU-23 for write access
├─ Check CRC on written frame (use Modbus analyzer)
│  └─ If CRC wrong → Verify calculation in firmware
└─ Confirm emergency stop condition is not latched
   └─ May require manual reset on GSR-CU-23 display
```

---

## Rollback Procedure

If issues arise in production:

1. **Restore previous iot_config.json:** Replace with last known-good version
2. **Clear Azure Device Twin:** Remove all custom properties
3. **Reboot ESP32:** Power cycle to reload from SD card
4. **Verify basic operation:** Confirm telemetry resumed

**Note:** Always keep backups of working configurations!

---

## Success Criteria

✅ **Deployment is successful when:**

- Telemetry arrives in Azure every X seconds (as configured)
- Register values are reasonable (no NaN, no wild swings)
- Error status flags show no critical errors
- Pressure readings match physical gauges on dispenser (±5% tolerance)
- Quantity increases over time during dispensing operations
- No CRC errors in 24+ hour uptime test
- Emergency stop control functional (if implemented)
- Alerts trigger appropriately for error conditions

---

## Contact & Support

- **GSR-CU-23 technical questions:** Refer to Gasorex manufacturer documentation
- **ESP32 firmware issues:** Check repository logs and documentation
- **Azure IoT Hub issues:** Consult Azure troubleshooting guide
- **Register mapping questions:** Refer to [PROTOCOL.md](PROTOCOL.md) and [register_map.csv](register_map.csv)

---

**Deployment Date: _____________**  
**Tested By: _____________**  
**Production Sign-Off: _____________**
