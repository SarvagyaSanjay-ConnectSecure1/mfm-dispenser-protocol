#include "compac_protocol_handler.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ========================================================================
   CRC-16 Modbus Implementation
   ======================================================================== */

uint16_t compac_crc16_modbus(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xA001;  // Reflected polynomial
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc;
}

/* ========================================================================
   16-bit Register Parsing
   ======================================================================== */

uint16_t compac_parse_uint16_be(const uint8_t *reg_bytes)
{
    return ((uint16_t)reg_bytes[0] << 8) | reg_bytes[1];
}

int16_t compac_parse_int16_be(const uint8_t *reg_bytes)
{
    return (int16_t)(((uint16_t)reg_bytes[0] << 8) | reg_bytes[1]);
}

/* ========================================================================
   32-bit Register Pair Parsing
   ======================================================================== */

uint32_t compac_parse_uint32_be(const uint8_t *high_word_bytes,
                                const uint8_t *low_word_bytes)
{
    uint32_t high_word = ((uint32_t)high_word_bytes[0] << 8) | high_word_bytes[1];
    uint32_t low_word = ((uint32_t)low_word_bytes[0] << 8) | low_word_bytes[1];
    
    return (high_word << 16) | low_word;
}

/* ========================================================================
   BCD12 Parsing
   ======================================================================== */

bool compac_parse_bcd12(const uint8_t *bcd_bytes, char *bcd_string)
{
    if (!bcd_bytes || !bcd_string) {
        return false;
    }
    
    int idx = 0;
    
    for (int i = 0; i < 6; i++) {
        uint8_t byte = bcd_bytes[i];
        uint8_t high_nibble = (byte >> 4) & 0x0F;
        uint8_t low_nibble = byte & 0x0F;
        
        /* Validate BCD digits (0-9) */
        if (high_nibble > 9 || low_nibble > 9) {
            return false;
        }
        
        bcd_string[idx++] = '0' + high_nibble;
        bcd_string[idx++] = '0' + low_nibble;
    }
    
    bcd_string[12] = '\0';  /* Null terminate */
    return true;
}

double compac_bcd12_to_double(const char *bcd_string, double scale)
{
    if (!bcd_string) {
        return 0.0;
    }
    
    /* Parse 12-digit BCD string as unsigned 64-bit */
    uint64_t numeric = 0;
    
    for (int i = 0; i < 12; i++) {
        if (bcd_string[i] < '0' || bcd_string[i] > '9') {
            return 0.0;
        }
        numeric = numeric * 10 + (bcd_string[i] - '0');
    }
    
    return (double)numeric * scale;
}

/* ========================================================================
   Status Bits Decoding
   ======================================================================== */

void compac_decode_status_bits(uint16_t status_word, compac_status_bits_t *status)
{
    if (!status) {
        return;
    }
    
    status->push_to_start_pressed = (status_word & 0x0001) != 0;
    status->stop_button_active    = (status_word & 0x0002) != 0;
    status->mfm_running           = (status_word & 0x0004) != 0;
    status->dispenser_tripped     = (status_word & 0x0008) != 0;
    status->bank1_active          = (status_word & 0x0010) != 0;
    status->bank2_active          = (status_word & 0x0020) != 0;
    status->bank3_active          = (status_word & 0x0040) != 0;
}

/* ========================================================================
   Error Flags Decoding
   ======================================================================== */

void compac_decode_error_flags(uint16_t error_word, compac_error_flags_t *errors)
{
    if (!errors) {
        return;
    }
    
    errors->raw_flags = error_word;
    errors->reverse_flow_detected      = (error_word & 0x0001) != 0;
    errors->mfm_fault                  = (error_word & 0x0002) != 0;
    errors->temp_pressure_board_fault  = (error_word & 0x0004) != 0;
    errors->pressure_probe_fault       = (error_word & 0x0008) != 0;
    errors->temperature_probe_fault    = (error_word & 0x0010) != 0;
    errors->overpressure_warning       = (error_word & 0x0020) != 0;
    errors->underpressure_warning      = (error_word & 0x0040) != 0;
}

/* ========================================================================
   Complete Telemetry Parsing
   ======================================================================== */

int compac_parse_telemetry(const uint8_t *response_frame, size_t response_length,
                          compac_telemetry_t *telemetry)
{
    if (!response_frame || !telemetry || response_length < 35) {
        return COMPAC_PARSE_SHORT_FRAME;
    }
    
    /* Frame structure:
       [0] Slave ID
       [1] Function Code (should be 0x03)
       [2] Byte Count (should be 32)
       [3-34] Register Data (16 registers × 2 bytes = 32 bytes)
       [35] CRC Low
       [36] CRC High
    */
    
    uint8_t slave_id = response_frame[0];
    uint8_t function_code = response_frame[1];
    uint8_t byte_count = response_frame[2];
    
    /* Validate frame */
    if (function_code != 0x03) {
        return COMPAC_PARSE_INVALID_FC;
    }
    
    if (byte_count != 32) {
        return COMPAC_PARSE_SHORT_FRAME;
    }
    
    /* Verify CRC */
    uint16_t received_crc = ((uint16_t)response_frame[response_length - 1] << 8) |
                           response_frame[response_length - 2];
    uint16_t calculated_crc = compac_crc16_modbus(response_frame, response_length - 2);
    
    if (received_crc != calculated_crc) {
        return COMPAC_PARSE_INVALID_CRC;
    }
    
    /* Extract data pointer (start of register data) */
    const uint8_t *data = &response_frame[3];
    
    /* Initialize telemetry */
    memset(telemetry, 0, sizeof(compac_telemetry_t));
    telemetry->slave_address = slave_id;
    telemetry->data_valid = false;
    
    /* Parse registers 0x0100-0x010F (offset 0-31 in data) */
    
    /* Register 0x0100 (offset 0-1): Status bits */
    uint16_t status_raw = compac_parse_uint16_be(&data[0]);
    compac_decode_status_bits(status_raw, &telemetry->status);
    
    /* Register 0x0101 (offset 2-3): Unit price */
    uint16_t unit_price_raw = compac_parse_uint16_be(&data[2]);
    telemetry->delivery.unit_price = unit_price_raw * 0.01f;
    
    /* Registers 0x0102-0x0103 (offset 4-7): Current delivery quantity (U32) */
    uint32_t qty_raw = compac_parse_uint32_be(&data[4], &data[6]);
    telemetry->delivery.quantity_kg = qty_raw * 0.001;
    
    /* Registers 0x0104-0x0105 (offset 8-11): Current delivery amount (U32) */
    uint32_t amt_raw = compac_parse_uint32_be(&data[8], &data[10]);
    telemetry->delivery.amount_currency = amt_raw * 0.01;
    
    /* Registers 0x0106-0x0108 (offset 12-17): Quantity totalizer (BCD12) */
    if (!compac_parse_bcd12(&data[12], telemetry->totalizer.quantity_bcd_string)) {
        return COMPAC_PARSE_BCD_ERROR;
    }
    telemetry->totalizer.quantity_kg = 
        compac_bcd12_to_double(telemetry->totalizer.quantity_bcd_string, 0.001);
    
    /* Registers 0x0109-0x010B (offset 18-23): Monetary totalizer (BCD12) */
    if (!compac_parse_bcd12(&data[18], telemetry->totalizer.amount_bcd_string)) {
        return COMPAC_PARSE_BCD_ERROR;
    }
    telemetry->totalizer.amount_currency = 
        compac_bcd12_to_double(telemetry->totalizer.amount_bcd_string, 0.01);
    
    telemetry->totalizer.bcd_parse_valid = true;
    
    /* Register 0x0112 (offset 24-25): Pressure */
    uint16_t pressure_raw = compac_parse_uint16_be(&data[24]);
    telemetry->pressure.dispenser_pressure_bar = pressure_raw * 0.01f;
    
    /* Register 0x0113 (offset 26-27): Flow rate */
    uint16_t flowrate_raw = compac_parse_uint16_be(&data[26]);
    telemetry->flow.flow_rate_kg_per_min = flowrate_raw * 0.01f;
    
    /* Register 0x0114 (offset 28-29): Error flags */
    uint16_t error_raw = compac_parse_uint16_be(&data[28]);
    compac_decode_error_flags(error_raw, &telemetry->errors);
    
    /* Register 0x0115 (offset 30-31): Ambient temperature (INT16) */
    int16_t ambient_temp_raw = compac_parse_int16_be(&data[30]);
    telemetry->temperature.ambient_temperature_c = ambient_temp_raw * 0.1f;
    
    /* Register 0x0116 would be at offset 32, but we only have 32 bytes (0-31)
       This is a read of 16 registers (0x0100-0x010F), which covers up to 0x010F
       So 0x0116 is NOT included in standard 16-register read.
       For gas temperature, additional read needed or configuration adjustment.
    */
    telemetry->temperature.gas_temperature_c = 0.0f; /* Not in this frame */
    
    telemetry->data_valid = true;
    return COMPAC_PARSE_OK;
}

/* ========================================================================
   Frame Construction (Write Operations)
   ======================================================================== */

/**
 * Generic write single register frame builder
 */
static int build_write_single_register(uint8_t slave_address, uint16_t register_address,
                                      uint16_t register_value, uint8_t *frame_buffer,
                                      size_t *frame_length)
{
    if (*frame_length < 8) {
        return COMPAC_BUILD_BUFFER_SMALL;
    }
    
    frame_buffer[0] = slave_address;
    frame_buffer[1] = 0x06;  /* Function Code 06 */
    frame_buffer[2] = (register_address >> 8) & 0xFF;
    frame_buffer[3] = register_address & 0xFF;
    frame_buffer[4] = (register_value >> 8) & 0xFF;
    frame_buffer[5] = register_value & 0xFF;
    
    /* Calculate CRC */
    uint16_t crc = compac_crc16_modbus(frame_buffer, 6);
    frame_buffer[6] = crc & 0xFF;           /* CRC Low */
    frame_buffer[7] = (crc >> 8) & 0xFF;    /* CRC High */
    
    *frame_length = 8;
    return COMPAC_BUILD_OK;
}

int compac_build_price_update_frame(uint8_t slave_address, uint16_t price_value,
                                    uint8_t *frame_buffer, size_t *frame_length)
{
    return build_write_single_register(slave_address, 0x0201, price_value,
                                      frame_buffer, frame_length);
}

int compac_build_lock_control_frame(uint8_t slave_address, uint8_t lock_state,
                                    uint8_t *frame_buffer, size_t *frame_length)
{
    uint16_t lock_value = (lock_state != 0) ? 1 : 0;
    return build_write_single_register(slave_address, 0x0200, lock_value,
                                      frame_buffer, frame_length);
}

int compac_build_set_target_pressure_frame(uint8_t slave_address, uint16_t pressure_value,
                                          uint8_t *frame_buffer, size_t *frame_length)
{
    return build_write_single_register(slave_address, 0x0202, pressure_value,
                                      frame_buffer, frame_length);
}
