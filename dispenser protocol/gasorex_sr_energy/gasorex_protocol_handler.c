#include "gasorex_protocol_handler.h"
#include <math.h>
#include <stdlib.h>

/* ── Float parsing from big-endian 16-bit pairs ────────────────────────── */

float gasorex_parse_float32_be(const uint8_t *reg_high_bytes, const uint8_t *reg_low_bytes)
{
    if (!reg_high_bytes || !reg_low_bytes) return 0.0f;

    uint16_t high = ((uint16_t)reg_high_bytes[0] << 8) | reg_high_bytes[1];
    uint16_t low = ((uint16_t)reg_low_bytes[0] << 8) | reg_low_bytes[1];

    uint32_t combined = ((uint32_t)high << 16) | low;
    float result = *(float *)&combined;

    return isnan(result) ? 0.0f : result;
}

/* ── 16-bit integer parsing from big-endian ────────────────────────────── */

uint16_t gasorex_parse_uint16_be(const uint8_t *reg_bytes)
{
    if (!reg_bytes) return 0;
    return ((uint16_t)reg_bytes[0] << 8) | reg_bytes[1];
}

int16_t gasorex_parse_int16_be(const uint8_t *reg_bytes)
{
    if (!reg_bytes) return 0;
    uint16_t u = ((uint16_t)reg_bytes[0] << 8) | reg_bytes[1];
    return (int16_t)u;
}

/* ── Parse Side A (registers 1–14, ~28 bytes) ──────────────────────────── */

static void parse_side_a(const uint8_t *data, size_t len, gasorex_side_t *out)
{
    if (!data || !out) return;
    memset(out, 0, sizeof(*out));

    if (len < 28) return;  // Need at least 14 registers × 2 bytes

    int offset = 0;
    
    // Reg 1–2: Amount (Float, Rs)
    out->amount_rs = gasorex_parse_float32_be(&data[offset], &data[offset + 2]);
    offset += 4;

    // Reg 3–4: Quantity (Float, Kg/L)
    out->quantity_kg = gasorex_parse_float32_be(&data[offset], &data[offset + 2]);
    offset += 4;

    // Reg 5–6: Electronic Totalizer (Float, Rs)
    out->electronic_totalizer_rs = gasorex_parse_float32_be(&data[offset], &data[offset + 2]);
    offset += 4;

    // Reg 7–8: Flowmeter Totalizer (Float, Kg/L)
    out->flowmeter_totalizer_kg = gasorex_parse_float32_be(&data[offset], &data[offset + 2]);
    offset += 4;

    // Reg 9: PT1 Pressure (U16, bar, scale 0.1)
    uint16_t pt1_raw = gasorex_parse_uint16_be(&data[offset]);
    out->pt1_pressure_bar = pt1_raw * 0.1f;
    offset += 2;

    // Reg 10: PT2 Pressure (U16, bar, scale 0.1)
    uint16_t pt2_raw = gasorex_parse_uint16_be(&data[offset]);
    out->pt2_pressure_bar = pt2_raw * 0.1f;
    offset += 2;

    // Reg 11–12: Flowrate (Float, kg/min)
    out->flowrate_kg_per_min = gasorex_parse_float32_be(&data[offset], &data[offset + 2]);
    offset += 4;

    // Reg 13: Error Status (U16, bit flags)
    out->error_status = gasorex_parse_uint16_be(&data[offset]);
    offset += 2;

    // Reg 14: Emergency Stop Control (U16, bool)
    uint16_t estop = gasorex_parse_uint16_be(&data[offset]);
    out->emergency_stop_active = (estop != 0);
    offset += 2;

    // Optional Reg 15: Temperature (I16, °C, scale 0.1)
    if (len >= 30) {
        int16_t temp_raw = gasorex_parse_int16_be(&data[offset]);
        out->temperature_celsius = temp_raw * 0.1f;
    }
}

/* ── Parse Side B (registers 16–30, ~30 bytes) ─────────────────────────── */

static void parse_side_b(const uint8_t *data, size_t len, gasorex_side_t *out)
{
    if (!data || !out) return;
    memset(out, 0, sizeof(*out));

    if (len < 28) return;  // Need at least 14 registers × 2 bytes

    int offset = 0;

    // Reg 16–17: Amount (Float, Rs)
    out->amount_rs = gasorex_parse_float32_be(&data[offset], &data[offset + 2]);
    offset += 4;

    // Reg 18–19: Quantity (Float, Kg/L)
    out->quantity_kg = gasorex_parse_float32_be(&data[offset], &data[offset + 2]);
    offset += 4;

    // Reg 20–21: Electronic Totalizer (Float, Rs)
    out->electronic_totalizer_rs = gasorex_parse_float32_be(&data[offset], &data[offset + 2]);
    offset += 4;

    // Reg 22–23: Flowmeter Totalizer (Float, Kg/L)
    out->flowmeter_totalizer_kg = gasorex_parse_float32_be(&data[offset], &data[offset + 2]);
    offset += 4;

    // Reg 24: PT3 Pressure (Side B PT1) (U16, bar, scale 0.1)
    uint16_t pt3_raw = gasorex_parse_uint16_be(&data[offset]);
    out->pt1_pressure_bar = pt3_raw * 0.1f;  // Labeled as PT1 for consistency
    offset += 2;

    // Reg 25: PT4 Pressure (Side B PT2) (U16, bar, scale 0.1)
    uint16_t pt4_raw = gasorex_parse_uint16_be(&data[offset]);
    out->pt2_pressure_bar = pt4_raw * 0.1f;  // Labeled as PT2 for consistency
    offset += 2;

    // Reg 26–27: Flowrate (Float, kg/min)
    out->flowrate_kg_per_min = gasorex_parse_float32_be(&data[offset], &data[offset + 2]);
    offset += 4;

    // Reg 28: Error Status (U16, bit flags)
    out->error_status = gasorex_parse_uint16_be(&data[offset]);
    offset += 2;

    // Reg 29: Emergency Stop Control (U16, bool)
    uint16_t estop = gasorex_parse_uint16_be(&data[offset]);
    out->emergency_stop_active = (estop != 0);
    offset += 2;

    // Optional Reg 30: Temperature (I16, °C, scale 0.1)
    if (len >= 30) {
        int16_t temp_raw = gasorex_parse_int16_be(&data[offset]);
        out->temperature_celsius = temp_raw * 0.1f;
    }
}

/* ── Parse Pricing Registers (registers 31–34, 8 bytes) ──────────────────── */

static void parse_pricing(const uint8_t *data, size_t len, gasorex_telemetry_t *out)
{
    if (!data || !out || len < 8) {
        out->first_rate_rs_per_kg = 0.0f;
        out->second_rate_rs_per_kg = 0.0f;
        return;
    }

    // Reg 31–32: First Rate (Float, Rs/kg)
    out->first_rate_rs_per_kg = gasorex_parse_float32_be(&data[0], &data[2]);

    // Reg 33–34: Second Rate (Float, Rs/kg)
    out->second_rate_rs_per_kg = gasorex_parse_float32_be(&data[4], &data[6]);
}

/* ── Main telemetry parser ──────────────────────────────────────────────── */

bool gasorex_parse_telemetry(
    const uint8_t **raw_bufs,
    const size_t *raw_lens,
    int buf_count,
    gasorex_telemetry_t *out_telemetry)
{
    if (!raw_bufs || !raw_lens || !out_telemetry || buf_count < 2) {
        return false;
    }

    memset(out_telemetry, 0, sizeof(*out_telemetry));

    // Buffer 0: Side A (regs 1–14)
    if (raw_bufs[0] && raw_lens[0] >= 28) {
        parse_side_a(raw_bufs[0], raw_lens[0], &out_telemetry->side_a);
    } else {
        return false;
    }

    // Buffer 1: Side B (regs 16–30)
    if (raw_bufs[1] && raw_lens[1] >= 28) {
        parse_side_b(raw_bufs[1], raw_lens[1], &out_telemetry->side_b);
    } else {
        return false;
    }

    // Buffer 2 (optional): Pricing (regs 31–34)
    if (buf_count >= 3 && raw_bufs[2] && raw_lens[2] >= 8) {
        parse_pricing(raw_bufs[2], raw_lens[2], out_telemetry);
    }

    // Timestamp (in real implementation, use system clock)
    // out_telemetry->timestamp_ms = esp_timer_get_time() / 1000;

    return true;
}

/* ── Error flag decoding ────────────────────────────────────────────────── */

gasorex_error_flags_t gasorex_decode_error_flags(uint16_t error_status_raw)
{
    gasorex_error_flags_t flags = {0};

    flags.flowmeter_error = (error_status_raw & 0x0001) != 0;
    flags.pt_error = (error_status_raw & 0x0002) != 0;
    flags.solenoid_fault = (error_status_raw & 0x0004) != 0;
    flags.safety_interlock = (error_status_raw & 0x0008) != 0;
    flags.overtemp_warning = (error_status_raw & 0x0010) != 0;
    flags.underpressure_warning = (error_status_raw & 0x0020) != 0;
    flags.overpressure_warning = (error_status_raw & 0x0040) != 0;
    flags.control_module_fault = (error_status_raw & 0x0080) != 0;

    return flags;
}

/* ── CRC-16 Modbus RTU ──────────────────────────────────────────────────── */

static uint16_t crc16_modbus(const uint8_t *data, size_t len)
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

/* ── Build emergency stop frame ─────────────────────────────────────────── */

size_t gasorex_build_emergency_stop_frame(
    uint8_t slave_addr,
    char side,
    bool set_stop,
    uint8_t *out_frame,
    size_t frame_size)
{
    if (!out_frame || frame_size < 8) return 0;

    uint16_t register_addr;
    
    // Side A emergency stop = register 14, Side B = register 29
    if (side == 'A' || side == 'a') {
        register_addr = 14;
    } else if (side == 'B' || side == 'b') {
        register_addr = 29;
    } else {
        return 0;  // Invalid side
    }

    // Build Modbus RTU write single register (Function Code 06)
    out_frame[0] = slave_addr;
    out_frame[1] = 0x06;  // FC06: Write Single Register
    out_frame[2] = (register_addr >> 8) & 0xFF;
    out_frame[3] = register_addr & 0xFF;
    out_frame[4] = 0x00;  // Value high byte
    out_frame[5] = set_stop ? 0x01 : 0x00;  // Value low byte (1 = set, 0 = reset)

    // Calculate CRC
    uint16_t crc = crc16_modbus(out_frame, 6);
    out_frame[6] = crc & 0xFF;  // CRC low
    out_frame[7] = (crc >> 8) & 0xFF;  // CRC high

    return 8;  // Return frame size
}
