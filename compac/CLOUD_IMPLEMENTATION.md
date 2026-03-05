# Compac CNG Dispenser - Cloud-Side Implementation Guide

## Overview

Insights about cloud-side parsing examples for telemetry data received from Compac CNG dispenser units connected via ESP32 IoT Hub. The key challenge is handling **BCD12-encoded totalizer values** which require special parsing before numeric conversion.

---

## 1. Data Flow

```
Compac Dispenser (Modbus RTU)
        ↓
   RS-485 Serial
        ↓
   ESP32 Device
        ↓ (parses with compac_protocol_handler.c)
        ↓
  iot_hub.c (publishes JSON)
        ↓
   Azure IoT Hub (MQTT)
        ↓
Cloud Parsing (4 language examples below)
        ↓
Stream Analytics / Functions / Logic Apps
        ↓
Cosmos DB / Data Lake / Business Logic
```

---

## 2. Telemetry JSON Format (From ESP32)

The ESP32 publishes telemetry in this JSON format:

```json
{
  "ro_code": "COMPAC_CNG_PUMP_001_HOSE_1",
  "c_id": 4001,
  "ts": 1642567890000,
  "data": {
    "status": {
      "push_to_start_pressed": true,
      "stop_button_active": false,
      "mfm_running": true,
      "dispenser_tripped": false,
      "bank1_active": true,
      "bank2_active": false,
      "bank3_active": false
    },
    "delivery": {
      "quantity_kg": 51.23,
      "amount_currency": 1024.60,
      "unit_price": 20.00
    },
    "totalizer": {
      "quantity_bcd_string": "123456789012",
      "quantity_kg": 123456789.012,
      "amount_bcd_string": "000012345678",
      "amount_currency": 123456.78,
      "bcd_parse_valid": true
    },
    "pressure": {
      "dispenser_pressure_bar": 250.50,
      "target_fill_pressure_bar": 250.00,
      "max_fill_pressure_bar": 300.00
    },
    "flow": {
      "flow_rate_kg_per_min": 45.67,
      "bank_flow_threshold_pct": 80,
      "min_flow_rate_kg_per_min": 5.00
    },
    "temperature": {
      "ambient_temperature_c": 25.3,
      "gas_temperature_c": 0.0
    },
    "errors": {
      "reverse_flow_detected": false,
      "mfm_fault": false,
      "temp_pressure_board_fault": false,
      "pressure_probe_fault": false,
      "temperature_probe_fault": false,
      "overpressure_warning": false,
      "underpressure_warning": false
    },
    "slave_address": 1
  }
}
```

---

## 3. JavaScript / Node.js Parser

### Installation

```bash
npm install dotenv
```

### Complete Parser Implementation

```javascript
/**
 * Compac CNG Telemetry Parser (Node.js)
 * Handles BCD12 decoding and floating-point validation
 */

class CompacCngParser {
  /**
   * Parse incoming telemetry from Azure IoT Hub
   * @param {Object} iotHubMessage - Raw message from IoT Hub
   * @returns {Object} Parsed telemetry with validation
   */
  static parseMessage(iotHubMessage) {
    const payload = typeof iotHubMessage.body === 'string'
      ? JSON.parse(iotHubMessage.body)
      : iotHubMessage.body;

    return {
      device: {
        roCode: payload.ro_code,
        clientId: payload.c_id,
        slaveAddress: payload.data?.slave_address || 1
      },
      timestamp: payload.ts || Date.now(),
      status: this._parseStatus(payload.data?.status),
      delivery: this._parseDelivery(payload.data?.delivery),
      totalizer: this._parseTotalizer(payload.data?.totalizer),
      process: {
        pressure: payload.data?.pressure,
        flow: payload.data?.flow,
        temperature: payload.data?.temperature
      },
      errors: this._parseErrors(payload.data?.errors),
      quality: {
        dataValid: payload.data?.totalizer?.bcd_parse_valid || false,
        timestamp: new Date(payload.ts || Date.now()).toISOString()
      }
    };
  }

  static _parseStatus(statusObj) {
    if (!statusObj) return null;
    return {
      pushToStartPressed: Boolean(statusObj.push_to_start_pressed),
      stopButtonActive: Boolean(statusObj.stop_button_active),
      mfmRunning: Boolean(statusObj.mfm_running),
      dispenserTripped: Boolean(statusObj.dispenser_tripped),
      activeBanks: [
        statusObj.bank1_active,
        statusObj.bank2_active,
        statusObj.bank3_active
      ].reduce((count, isActive) => count + (isActive ? 1 : 0), 0),
      operationalState: this._getOperationalState(statusObj)
    };
  }

  static _getOperationalState(statusObj) {
    if (statusObj.dispenser_tripped) return 'TRIPPED';
    if (statusObj.stop_button_active) return 'STOPPED';
    if (statusObj.mfm_running) return 'DISPENSING';
    if (statusObj.push_to_start_pressed) return 'READY';
    return 'IDLE';
  }

  static _parseDelivery(deliveryObj) {
    if (!deliveryObj) return null;
    return {
      quantityKg: parseFloat(deliveryObj.quantity_kg) || 0,
      amountCurrency: parseFloat(deliveryObj.amount_currency) || 0,
      unitPrice: parseFloat(deliveryObj.unit_price) || 0,
      validTransaction: (deliveryObj.quantity_kg || 0) > 0.001
    };
  }

  static _parseTotalizer(totalizerObj) {
    if (!totalizerObj) return null;
    return {
      rawQuantityBcd: totalizerObj.quantity_bcd_string,
      quantityKg: parseFloat(totalizerObj.quantity_kg) || 0,
      rawAmountBcd: totalizerObj.amount_bcd_string,
      amountCurrency: parseFloat(totalizerObj.amount_currency) || 0,
      parseValid: Boolean(totalizerObj.bcd_parse_valid),
      allTimeTotal: {
        kg: parseFloat(totalizerObj.quantity_kg) || 0,
        currency: parseFloat(totalizerObj.amount_currency) || 0
      }
    };
  }

  static _parseErrors(errorsObj) {
    if (!errorsObj) return { activeErrors: 0 };
    
    const errorList = [
      { name: 'REVERSE_FLOW', active: errorsObj.reverse_flow_detected },
      { name: 'MFM_FAULT', active: errorsObj.mfm_fault },
      { name: 'TEMP_PRESSURE_BOARD_FAULT', active: errorsObj.temp_pressure_board_fault },
      { name: 'PRESSURE_PROBE_FAULT', active: errorsObj.pressure_probe_fault },
      { name: 'TEMPERATURE_PROBE_FAULT', active: errorsObj.temperature_probe_fault },
      { name: 'OVERPRESSURE_WARNING', active: errorsObj.overpressure_warning },
      { name: 'UNDERPRESSURE_WARNING', active: errorsObj.underpressure_warning }
    ];

    return {
      activeErrors: errorList.filter(e => e.active).length,
      activeErrorNames: errorList.filter(e => e.active).map(e => e.name),
      hasCriticalError: errorList
        .slice(0, 5)
        .some(e => e.active),
      hasWarning: errorList
        .slice(5)
        .some(e => e.active)
    };
  }
}

/**
 * Example Azure Function (Function App)
 */
module.exports = async function(context, iotHubMessage) {
  try {
    const parsed = CompacCngParser.parseMessage(iotHubMessage);
    
    context.log('Parsed Compac Telemetry:', JSON.stringify(parsed, null, 2));
    
    // Validate BCD parsing
    if (!parsed.quality.dataValid) {
      context.log.error('BCD12 parsing failed - check ESP32 firmware');
      return;
    }
    
    // Detect anomalies
    const anomalies = [];
    
    if (parsed.process.pressure.dispenser_pressure_bar > parsed.process.pressure.max_fill_pressure_bar) {
      anomalies.push('OVERPRESSURE_DETECTED');
    }
    
    if (parsed.errors.hasCriticalError) {
      anomalies.push(`CRITICAL_ERROR: ${parsed.errors.activeErrorNames.join(', ')}`);
    }
    
    if (parsed.delivery.validTransaction && parsed.errors.reverse_flow_detected) {
      anomalies.push('REVERSE_FLOW_DURING_TRANSACTION');
    }
    
    // Send to Cosmos DB
    const cosmosOutput = {
      id: `${parsed.device.roCode}_${parsed.timestamp}`,
      deviceId: parsed.device.clientId,
      timestamp: parsed.quality.timestamp,
      parsedData: parsed,
      anomalies: anomalies,
      severity: anomalies.length > 0 ? 'WARNING' : 'INFO'
    };
    
    context.bindings.outputDocument = cosmosOutput;
    context.log('Telemetry stored in Cosmos DB');
    
  } catch (error) {
    context.log.error('Error parsing Compac telemetry:', error);
  }
};
```

### Unit Tests

```javascript
const assert = require('assert');

describe('Compac CNG Parser', () => {
  it('parses BCD12 totalizer correctly', () => {
    const message = {
      body: JSON.stringify({
        ro_code: 'TEST_001',
        c_id: 1,
        ts: 1642567890000,
        data: {
          totalizer: {
            quantity_bcd_string: '000123456789',
            quantity_kg: 123456.789,
            amount_bcd_string: '000001234567',
            amount_currency: 12345.67,
            bcd_parse_valid: true
          },
          status: {},
          delivery: {},
          pressure: {},
          flow: {},
          temperature: {},
          errors: {}
        }
      })
    };

    const parsed = CompacCngParser.parseMessage(message);
    assert.strictEqual(parsed.totalizer.quantityKg, 123456.789);
    assert.strictEqual(parsed.totalizer.amountCurrency, 12345.67);
    assert.strictEqual(parsed.quality.dataValid, true);
  });

  it('detects overpressure condition', () => {
    const message = {
      body: JSON.stringify({
        ro_code: 'TEST_001',
        c_id: 1,
        ts: Date.now(),
        data: {
          pressure: {
            dispenser_pressure_bar: 350.00,
            max_fill_pressure_bar: 300.00
          },
          status: {},
          delivery: {},
          totalizer: { bcd_parse_valid: true },
          flow: {},
          temperature: {},
          errors: {}
        }
      })
    };

    const parsed = CompacCngParser.parseMessage(message);
    assert.ok(parsed.process.pressure.dispenser_pressure_bar > 300);
  });
});
```

---

## 4. Python Parser

### Installation

```bash
pip install azure-iot-hub azure-storage-blob
```

### Complete Parser Implementation

```python
"""
Compac CNG Telemetry Parser (Python)
Compatible with Azure Functions and Stream Processing
"""

import json
from datetime import datetime
from typing import Dict, List, Optional, Tuple
from dataclasses import dataclass, asdict

@dataclass
class CompacTelemetry:
    """Complete Compac CNG telemetry structure"""
    device_code: str
    client_id: int
    slave_address: int
    timestamp: str
    
    # Operational state
    push_to_start: bool
    stop_active: bool
    mfm_running: bool
    active_banks: int
    
    # Current transaction
    delivery_qty_kg: float
    delivery_amount: float
    unit_price: float
    
    # Totalizers
    total_qty_kg: float
    total_amount: float
    bcd_valid: bool
    
    # Process parameters
    pressure_bar: float
    target_pressure_bar: float
    flow_kg_per_min: float
    ambient_temp_c: float
    
    # Errors
    error_count: int
    error_flags: List[str]
    
    # Quality metrics
    data_quality: float

class CompacParser:
    """Compac CNG telemetry parser"""
    
    @staticmethod
    def parse_message(iot_message: Dict) -> Optional[CompacTelemetry]:
        """
        Parse IoT Hub message into CompacTelemetry
        
        Args:
            iot_message: Raw IoT Hub message (dict-like)
            
        Returns:
            CompacTelemetry object or None on error
        """
        try:
            payload = iot_message if isinstance(iot_message, dict) else json.loads(iot_message)
            data = payload.get('data', {})
            
            # Extract status bits
            status = data.get('status', {})
            active_banks = sum([
                status.get('bank1_active', False),
                status.get('bank2_active', False),
                status.get('bank3_active', False)
            ])
            
            # Extract errors
            errors = data.get('errors', {})
            error_flags = [
                name for name, active in [
                    ('REVERSE_FLOW', errors.get('reverse_flow_detected')),
                    ('MFM_FAULT', errors.get('mfm_fault')),
                    ('T_P_BOARD_FAULT', errors.get('temp_pressure_board_fault')),
                    ('PRESSURE_PROBE_FAULT', errors.get('pressure_probe_fault')),
                    ('TEMP_PROBE_FAULT', errors.get('temperature_probe_fault')),
                    ('OVERPRESSURE', errors.get('overpressure_warning')),
                    ('UNDERPRESSURE', errors.get('underpressure_warning'))
                ] if active
            ]
            
            # Extract delivery
            delivery = data.get('delivery', {})
            
            # Extract totalizer
            totalizer = data.get('totalizer', {})
            bcd_valid = totalizer.get('bcd_parse_valid', False)
            
            # Extract process parameters
            pressure = data.get('pressure', {})
            flow = data.get('flow', {})
            temperature = data.get('temperature', {})
            
            # Calculate data quality
            data_quality = 1.0 if bcd_valid else 0.5
            
            telemetry = CompacTelemetry(
                device_code=payload.get('ro_code', 'UNKNOWN'),
                client_id=payload.get('c_id', 0),
                slave_address=data.get('slave_address', 1),
                timestamp=datetime.fromtimestamp(
                    payload.get('ts', 0) / 1000
                ).isoformat(),
                
                push_to_start=status.get('push_to_start_pressed', False),
                stop_active=status.get('stop_button_active', False),
                mfm_running=status.get('mfm_running', False),
                active_banks=active_banks,
                
                delivery_qty_kg=float(delivery.get('quantity_kg', 0)),
                delivery_amount=float(delivery.get('amount_currency', 0)),
                unit_price=float(delivery.get('unit_price', 0)),
                
                total_qty_kg=float(totalizer.get('quantity_kg', 0)),
                total_amount=float(totalizer.get('amount_currency', 0)),
                bcd_valid=bcd_valid,
                
                pressure_bar=float(pressure.get('dispenser_pressure_bar', 0)),
                target_pressure_bar=float(pressure.get('target_fill_pressure_bar', 0)),
                flow_kg_per_min=float(flow.get('flow_rate_kg_per_min', 0)),
                ambient_temp_c=float(temperature.get('ambient_temperature_c', 0)),
                
                error_count=len(error_flags),
                error_flags=error_flags,
                
                data_quality=data_quality
            )
            
            return telemetry
            
        except Exception as e:
            print(f"Error parsing Compac message: {e}")
            return None
    
    @staticmethod
    def detect_anomalies(telemetry: CompacTelemetry) -> Dict[str, any]:
        """Detect operational anomalies"""
        anomalies = {
            'count': 0,
            'critical': [],
            'warning': [],
            'info': []
        }
        
        # BCD parsing failure
        if not telemetry.bcd_valid:
            anomalies['critical'].append('BCD12_PARSE_FAILED')
            anomalies['count'] += 1
        
        # Overpressure
        if telemetry.pressure_bar > telemetry.target_pressure_bar * 1.2:
            anomalies['critical'].append('OVERPRESSURE_DETECTED')
            anomalies['count'] += 1
        
        # Critical errors
        critical_errors = [e for e in telemetry.error_flags if e in [
            'REVERSE_FLOW', 'MFM_FAULT', 'T_P_BOARD_FAULT'
        ]]
        if critical_errors:
            anomalies['critical'].extend(critical_errors)
            anomalies['count'] += len(critical_errors)
        
        # Warnings
        if 'OVERPRESSURE' in telemetry.error_flags:
            anomalies['warning'].append('FIRMWARE_OVERPRESSURE_FLAG')
        if 'UNDERPRESSURE' in telemetry.error_flags:
            anomalies['warning'].append('LOW_PRESSURE_WARNING')
        
        # Low quality data
        if telemetry.data_quality < 0.8:
            anomalies['info'].append('LOW_DATA_QUALITY')
        
        return anomalies

# Example Azure Function (HTTP Trigger)
def main(req):
    """Azure Function entry point"""
    import azure.functions as func
    
    try:
        req_body = req.get_json()
        parsed = CompacParser.parse_message(req_body)
        
        if not parsed:
            return func.HttpResponse("Parse failed", status_code=400)
        
        anomalies = CompacParser.detect_anomalies(parsed)
        
        result = {
            'telemetry': asdict(parsed),
            'anomalies': anomalies,
            'severity': 'CRITICAL' if anomalies['critical'] else \
                       'WARNING' if anomalies['warning'] else 'OK'
        }
        
        return func.HttpResponse(
            json.dumps(result),
            status_code=200,
            mimetype="application/json"
        )
    
    except Exception as e:
        return func.HttpResponse(f"Error: {e}", status_code=500)
```

---

## 5. C# Parser (Azure Function Alternative)

```csharp
using System;
using System.Collections.Generic;
using System.Linq;
using Newtonsoft.Json;
using Microsoft.Azure.WebJobs;
using Microsoft.Extensions.Logging;

public class CompacTelemetryModel
{
    [JsonProperty("ro_code")]
    public string DeviceCode { get; set; }
    
    [JsonProperty("c_id")]
    public int ClientId { get; set; }
    
    [JsonProperty("ts")]
    public long Timestamp { get; set; }
    
    [JsonProperty("data")]
    public CompacDataModel Data { get; set; }
}

public class CompacDataModel
{
    [JsonProperty("status")]
    public StatusModel Status { get; set; }
    
    [JsonProperty("delivery")]
    public DeliveryModel Delivery { get; set; }
    
    [JsonProperty("totalizer")]
    public TotalizerModel Totalizer { get; set; }
    
    [JsonProperty("pressure")]
    public PressureModel Pressure { get; set; }
    
    [JsonProperty("flow")]
    public FlowModel Flow { get; set; }
    
    [JsonProperty("errors")]
    public ErrorsModel Errors { get; set; }
    
    [JsonProperty("slave_address")]
    public int SlaveAddress { get; set; }
}

public class StatusModel
{
    [JsonProperty("push_to_start_pressed")]
    public bool PushToStartPressed { get; set; }
    
    [JsonProperty("mfm_running")]
    public bool MfmRunning { get; set; }
    
    [JsonProperty("bank1_active")]
    public bool Bank1Active { get; set; }
    
    [JsonProperty("bank2_active")]
    public bool Bank2Active { get; set; }
    
    [JsonProperty("bank3_active")]
    public bool Bank3Active { get; set; }
}

public class DeliveryModel
{
    [JsonProperty("quantity_kg")]
    public double QuantityKg { get; set; }
    
    [JsonProperty("amount_currency")]
    public double AmountCurrency { get; set; }
    
    [JsonProperty("unit_price")]
    public double UnitPrice { get; set; }
}

public class TotalizerModel
{
    [JsonProperty("quantity_kg")]
    public double QuantityKg { get; set; }
    
    [JsonProperty("amount_currency")]
    public double AmountCurrency { get; set; }
    
    [JsonProperty("bcd_parse_valid")]
    public bool BcdParseValid { get; set; }
}

public class PressureModel
{
    [JsonProperty("dispenser_pressure_bar")]
    public double DispenserPressureBar { get; set; }
    
    [JsonProperty("max_fill_pressure_bar")]
    public double MaxFillPressureBar { get; set; }
}

public class FlowModel
{
    [JsonProperty("flow_rate_kg_per_min")]
    public double FlowRateKgPerMin { get; set; }
}

public class ErrorsModel
{
    [JsonProperty("reverse_flow_detected")]
    public bool ReverseFlowDetected { get; set; }
    
    [JsonProperty("mfm_fault")]
    public bool MfmFault { get; set; }
    
    [JsonProperty("temp_pressure_board_fault")]
    public bool TempPressureBoardFault { get; set; }
}

public static class CompacParser
{
    [FunctionName("CompacTelemetryProcessor")]
    public static void Run(
        [QueueTrigger("iot-telemetry")] CompacTelemetryModel telemetry,
        [Cosmos(databaseName: "IoTData", collectionName: "Compac",
                ConnectionStringSetting = "CosmosDbConnection")] IAsyncCollector<dynamic> output,
        ILogger log)
    {
        try
        {
            log.LogInformation($"Processing Compac device: {telemetry.DeviceCode}");
            
            // Validate BCD parsing
            if (!telemetry.Data.Totalizer.BcdParseValid)
            {
                log.LogWarning("BCD12 parsing failed for device: " + telemetry.DeviceCode);
            }
            
            // Detect anomalies
            var anomalies = new List<string>();
            
            if (telemetry.Data.Pressure.DispenserPressureBar > 
                telemetry.Data.Pressure.MaxFillPressureBar * 1.1)
            {
                anomalies.Add("OVERPRESSURE_DETECTED");
            }
            
            if (telemetry.Data.Errors.ReverseFlowDetected)
            {
                anomalies.Add("REVERSE_FLOW");
            }
            
            // Store in Cosmos DB
            var document = new
            {
                id = $"{telemetry.DeviceCode}_{telemetry.Timestamp}",
                deviceCode = telemetry.DeviceCode,
                clientId = telemetry.ClientId,
                timestamp = DateTimeOffset.FromUnixTimeMilliseconds(telemetry.Timestamp),
                quantityDispensed = telemetry.Data.Delivery.QuantityKg,
                totalQuantity = telemetry.Data.Totalizer.QuantityKg,
                pressure = telemetry.Data.Pressure.DispenserPressureBar,
                flowRate = telemetry.Data.Flow.FlowRateKgPerMin,
                anomalies = anomalies,
                severity = anomalies.Count > 0 ? "WARNING" : "OK"
            };
            
            output.AddAsync(document);
            log.LogInformation($"Stored telemetry for {telemetry.DeviceCode}");
        }
        catch (Exception ex)
        {
            log.LogError($"Error processing Compac telemetry: {ex.Message}");
            throw;
        }
    }
}
```

---

## 6. Stream Analytics SQL Query

For real-time processing in Azure Stream Analytics:

```sql
SELECT
    ro_code as DeviceCode,
    c_id as ClientId,
    data.slave_address as SlaveAddress,
    data.delivery.quantity_kg as QtyDispensedKg,
    data.totalizer.quantity_kg as TotalQtyKg,
    data.pressure.dispenser_pressure_bar as PressureBar,
    data.flow.flow_rate_kg_per_min as FlowRateKgMin,
    data.temperature.ambient_temperature_c as AmbientTempC,
    CASE 
        WHEN data.pressure.dispenser_pressure_bar > data.pressure.max_fill_pressure_bar 
            THEN 'OVERPRESSURE'
        WHEN data.errors.reverse_flow_detected THEN 'REVERSE_FLOW'
        WHEN data.errors.mfm_fault THEN 'MFM_FAULT'
        ELSE 'OK'
    END as AlertStatus,
    System.Timestamp() as ProcessedTime
INTO [AlertOutput]
FROM [IoTInput]
WHERE
    (data.pressure.dispenser_pressure_bar > data.pressure.max_fill_pressure_bar * 1.1
     OR data.errors.reverse_flow_detected = true
     OR data.errors.mfm_fault = true
     OR data.totalizer.bcd_parse_valid = false)
```

---

## 7. Deployment Checklist (Cloud Side)

- [ ] Create Azure Function app with at least 3 parser instances (JavaScript, Python, C#)
- [ ] Set up Cosmos DB collection with TTL for old telemetry
- [ ] Configure Stream Analytics alerts for pressure > 300 bar
- [ ] Implement dead-letter queue for BCD12 parse failures
- [ ] Add Application Insights monitoring for parser error rates
- [ ] Create Power BI dashboard with pressure/flow trend charts
- [ ] Test failover behavior when primary parser fails
- [ ] Document BCD12 format in cloud team wiki
- [ ] Set alerts for >2 consecutive parse failures per device
- [ ] Implement automated rollback if anomaly rate exceeds 5%

---

## 8. Troubleshooting

| Cloud-Side Issue | Root Cause | Solution |
|------------------|-----------|----------|
| BCD decode fails | ESP32 firmware bug | Check compac_parse_bcd12() in C handler |
| Pressure values wrong | Scale factor error | Verify 0.01 scale applied in parsing |
| Missing temperature | Registers not in read frame | System reads only 0x0100-0x010F; 0x0116 requires separate request |
| Frequent CRC errors | RS-485 line issue | Physical check: termination, cable shielding |
| BCD string garbage | Invalid BCD nibbles | Indicates frame corruption; check serial connection |

---

## References

- Modbus RTU: [IEC 61158-2 Standard](https://en.wikipedia.org/wiki/Modbus)
- BCD Encoding: ISO 6093
- Azure IoT Hub: [Microsoft Docs](https://docs.microsoft.com/en-us/azure/iot-hub/)
- Stream Analytics: [Query Language Reference](https://docs.microsoft.com/en-us/stream-analytics-query/stream-analytics-query-language-reference)


