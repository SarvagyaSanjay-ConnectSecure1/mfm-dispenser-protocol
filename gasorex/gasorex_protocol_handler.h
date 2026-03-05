#pragma once
#ifndef __GASOREX_PROTOCOL_HANDLER_H__
#define __GASOREX_PROTOCOL_HANDLER_H__

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/**
 * Gasorex SR Energy Protocol Handler
 * 
 * This module provides parsing and interpretation of raw Modbus RTU data
 * from the Gasorex SR Energy GSR-CU-23 controller unit.
 * 
 * Raw data is read via modbus_adapter as binary streams, and this handler
 * converts them into business metrics (volume, pressure, flowrate, etc.)
 */

typedef struct {
    float amount_rs;                // Rs of current transaction
    float quantity_kg;              // Kg dispensed in current transaction
    float electronic_totalizer_rs;  // Cumulative Rs (electronic)
    float flowmeter_totalizer_kg;   // Cumulative Kg (flowmeter)
    float pt1_pressure_bar;         // Pressure transmitter 1 (bar)
    float pt2_pressure_bar;         // Pressure transmitter 2 (bar)
    float flowrate_kg_per_min;      // Current flowrate (kg/min)
    uint16_t error_status;          // Bit flags for errors/warnings
    bool emergency_stop_active;     // Emergency stop status
    float temperature_celsius;      // Optional pump/solenoid temperature
} gasorex_side_t;

typedef struct {
    gasorex_side_t side_a;
    gasorex_side_t side_b;
    float first_rate_rs_per_kg;     // Pricing: Rate 1
    float second_rate_rs_per_kg;    // Pricing: Rate 2
    uint64_t timestamp_ms;          // Timestamp of parse
} gasorex_telemetry_t;

/**
 * Parse raw Modbus register data into Gasorex telemetry structure
 * 
 * @param raw_bufs Array of raw byte buffers (one per register range)
 * @param raw_lens Array of byte lengths corresponding to each buffer
 * @param buf_count Number of buffers in raw_bufs
 * @param out_telemetry Output telemetry structure
 * @return true if parsing succeeded, false otherwise
 * 
 * Expected buffer layout:
 *   buf[0] = registers 1-14 (Side A, 28 bytes)
 *   buf[1] = registers 16-30 (Side B, 30 bytes)
 *   buf[2] = registers 31-34 (pricing, 8 bytes) [optional]
 */
bool gasorex_parse_telemetry(
    const uint8_t **raw_bufs,
    const size_t *raw_lens,
    int buf_count,
    gasorex_telemetry_t *out_telemetry
);

/**
 * Helper: Parse a 32-bit float from two consecutive 16-bit Modbus registers
 * (big-endian format per Modbus RTU standard)
 * 
 * @param reg_high_bytes Pointer to first register (2 bytes, big-endian)
 * @param reg_low_bytes Pointer to second register (2 bytes, big-endian)
 * @return Parsed float value
 */
float gasorex_parse_float32_be(const uint8_t *reg_high_bytes, const uint8_t *reg_low_bytes);

/**
 * Helper: Parse a 16-bit unsigned integer from Modbus register (big-endian)
 * 
 * @param reg_bytes Pointer to register (2 bytes, big-endian)
 * @return Parsed uint16 value
 */
uint16_t gasorex_parse_uint16_be(const uint8_t *reg_bytes);

/**
 * Helper: Parse a 16-bit signed integer from Modbus register (big-endian)
 * 
 * @param reg_bytes Pointer to register (2 bytes, big-endian)
 * @return Parsed int16 value
 */
int16_t gasorex_parse_int16_be(const uint8_t *reg_bytes);

/**
 * Build a Modbus RTU frame to set emergency stop on a side
 * 
 * @param slave_addr Modbus slave address
 * @param side 'A' or 'B'
 * @param set_stop true to set emergency stop, false to reset
 * @param out_frame Buffer to hold the constructed frame
 * @param frame_size Maximum size of out_frame
 * @return Size of constructed frame, or 0 on error
 */
size_t gasorex_build_emergency_stop_frame(
    uint8_t slave_addr,
    char side,
    bool set_stop,
    uint8_t *out_frame,
    size_t frame_size
);

/**
 * Decode error status flags from Gasorex response
 * 
 * @param error_status_raw 16-bit error status word
 * @return Struct with individual bit flags extracted
 */
typedef struct {
    bool flowmeter_error;
    bool pt_error;
    bool solenoid_fault;
    bool safety_interlock;
    bool overtemp_warning;
    bool underpressure_warning;
    bool overpressure_warning;
    bool control_module_fault;
} gasorex_error_flags_t;

gasorex_error_flags_t gasorex_decode_error_flags(uint16_t error_status_raw);

#endif // __GASOREX_PROTOCOL_HANDLER_H__
