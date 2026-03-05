#ifndef COMPAC_PROTOCOL_HANDLER_H
#define COMPAC_PROTOCOL_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @file compac_protocol_handler.h
 * @brief Compac CNG Dispenser Protocol Handler
 *
 * This module provides protocol parsing and frame construction for Compac CNG
 * dispenser systems communicating via Modbus RTU over RS-485.
 * 
 * Key Features:
 * - Multi-nozzle support (each nozzle = independent slave)
 * - BCD12 totalizer parsing (6-byte binary-coded decimal)
 * - 9600-8N1 (no parity) communication
 * - 100ms minimum polling interval requirement
 * - CRC-16 Modbus validation
 */

/* ========================================================================
   Data Structure Definitions
   ======================================================================== */

/**
 * @typedef compac_pressure_data_t
 * @brief Pressure readings from dispenser
 */
typedef struct {
    float dispenser_pressure_bar;     ///< Current outlet pressure (0.01 scale)
    float target_fill_pressure_bar;   ///< Target finish pressure
    float max_fill_pressure_bar;      ///< Safety limit pressure
} compac_pressure_data_t;

/**
 * @typedef compac_flow_data_t
 * @brief Flow measurement data
 */
typedef struct {
    float flow_rate_kg_per_min;       ///< Current flow rate (0.01 scale)
    uint16_t bank_flow_threshold_pct; ///< Bank switching flow threshold (%)
    float min_flow_rate_kg_per_min;   ///< Minimum for high bank (0.01 scale)
} compac_flow_data_t;

/**
 * @typedef compac_temperature_data_t
 * @brief Temperature measurements
 */
typedef struct {
    float ambient_temperature_c;      ///< Ambient/external temp (0.1 scale, signed)
    float gas_temperature_c;          ///< Gas outlet temp (0.1 scale, signed)
} compac_temperature_data_t;

/**
 * @typedef compac_status_bits_t
 * @brief Status register bit flags (0x0100)
 */
typedef struct {
    bool push_to_start_pressed;       ///< Bit 0
    bool stop_button_active;          ///< Bit 1 (1=stopped)
    bool mfm_running;                 ///< Bit 2 (mass flow meter status)
    bool dispenser_tripped;           ///< Bit 3
    bool bank1_active;                ///< Bit 4 (low pressure)
    bool bank2_active;                ///< Bit 5 (medium pressure)
    bool bank3_active;                ///< Bit 6 (high pressure)
} compac_status_bits_t;

/**
 * @typedef compac_error_flags_t
 * @brief Error status register bits (0x0114)
 */
typedef struct {
    bool reverse_flow_detected;       ///< Bit 0
    bool mfm_fault;                   ///< Bit 1
    bool temp_pressure_board_fault;   ///< Bit 2 (T/P board disconnected)
    bool pressure_probe_fault;        ///< Bit 3
    bool temperature_probe_fault;     ///< Bit 4
    bool overpressure_warning;        ///< Bit 5
    bool underpressure_warning;       ///< Bit 6
    uint16_t raw_flags;               ///< Full register value for custom decoding
} compac_error_flags_t;

/**
 * @typedef compac_delivery_data_t
 * @brief Current transaction delivery information
 */
typedef struct {
    double quantity_kg;               ///< Quantity dispensed (0.001 scale from U32)
    double amount_currency;           ///< Monetary value (0.01 scale from U32)
    float unit_price;                 ///< Current unit price (0.01 scale)
} compac_delivery_data_t;

/**
 * @typedef compac_totalizer_data_t
 * @brief Cumulative totalizer values (BCD12 encoded)
 */
typedef struct {
    char quantity_bcd_string[13];     ///< 12-digit BCD string (null-terminated)
    double quantity_kg;               ///< Parsed quantity (0.001 scale)
    
    char amount_bcd_string[13];       ///< 12-digit BCD string (null-terminated)
    double amount_currency;           ///< Parsed amount (0.01 scale)
    
    bool bcd_parse_valid;             ///< Indicates BCD parsing succeeded
} compac_totalizer_data_t;

/**
 * @typedef compac_telemetry_t
 * @brief Complete telemetry snapshot from one hose/nozzle
 */
typedef struct {
    /* Status and operational */
    compac_status_bits_t status;
    compac_error_flags_t errors;
    bool dispenser_locked;
    
    /* Current delivery */
    compac_delivery_data_t delivery;
    
    /* Totalizers (cumulative) */
    compac_totalizer_data_t totalizer;
    
    /* Process parameters */
    compac_pressure_data_t pressure;
    compac_flow_data_t flow;
    compac_temperature_data_t temperature;
    
    /* Metadata */
    uint16_t slave_address;           ///< Modbus slave ID (01-99)
    uint32_t last_read_timestamp_ms;  ///< Timestamp of last successful read
    bool data_valid;                  ///< Indicates complete valid parse
} compac_telemetry_t;

/* ========================================================================
   BCD12 Parsing Functions
   ======================================================================== */

/**
 * Parse a 6-byte BCD12 encoded value into ASCII string
 * 
 * @param[in] bcd_bytes Buffer containing 6 bytes of BCD data
 * @param[out] bcd_string Output string (must be 13 bytes: 12 digits + null)
 * @return true if parsing succeeded, false on invalid BCD digits
 * 
 * Example:
 *   uint8_t bcd_data[6] = {0x12, 0x34, 0x56, 0x78, 0x90, 0x12};
 *   char result[13];
 *   compac_parse_bcd12(bcd_data, result);
 *   // result = "123456789012"
 */
bool compac_parse_bcd12(const uint8_t *bcd_bytes, char *bcd_string);

/**
 * Convert BCD12 string to double with scaling
 * 
 * @param[in] bcd_string 12-digit BCD string from compac_parse_bcd12()
 * @param[in] scale Scaling factor (0.001 for kg, 0.01 for currency)
 * @return Parsed numeric value with scaling applied
 * 
 * Example:
 *   "000123456789" with scale 0.001 = 123456.789
 */
double compac_bcd12_to_double(const char *bcd_string, double scale);

/* ========================================================================
   Data Extraction Functions (16-bit registers)
   ======================================================================== */

/**
 * Parse 16-bit unsigned integer (big-endian)
 * 
 * @param[in] reg_bytes 2-byte register data
 * @return Parsed 16-bit value
 */
uint16_t compac_parse_uint16_be(const uint8_t *reg_bytes);

/**
 * Parse 16-bit signed integer (big-endian, two's complement)
 * 
 * @param[in] reg_bytes 2-byte register data
 * @return Parsed 16-bit signed value
 */
int16_t compac_parse_int16_be(const uint8_t *reg_bytes);

/* ========================================================================
   32-bit Register Pair Parsing
   ======================================================================== */

/**
 * Parse 32-bit unsigned from register pair (big-endian)
 * Registers are in format: [high_word_byte0, high_word_byte1, low_word_byte0, low_word_byte1]
 * 
 * @param[in] high_word_bytes First 2 bytes (high word)
 * @param[in] low_word_bytes Next 2 bytes (low word)
 * @return Combined 32-bit value
 * 
 * Example:
 *   Quantity at 0102-0103: [0x00, 0xC6, 0x1C, 0x40]
 *   Result: 0x00C61C40 = 205652544 (register units)
 *   With scale 0.001: 205652.544 kg
 */
uint32_t compac_parse_uint32_be(const uint8_t *high_word_bytes,
                                const uint8_t *low_word_bytes);

/* ========================================================================
   Telemetry Parsing (Full Frame)
   ======================================================================== */

/**
 * Parse complete Modbus RTU response (16 registers: 0x0100-0x010F)
 * 
 * @param[in] response_frame Modbus RTU response frame (entire frame including slave ID, FC, byte count, CRC)
 * @param[in] response_length Total frame length in bytes (minimum 33 bytes)
 * @param[out] telemetry Parsed telemetry structure
 * @return COMPAC_PARSE_OK on success, error code otherwise
 * 
 * Assumes standard read response for 16 registers (32 bytes data):
 *   [Slave] [FC=3] [ByteCount=32] [Register Data (32 bytes)] [CRC-L] [CRC-H]
 *    1       1      1             32                          1       1
 */
int compac_parse_telemetry(const uint8_t *response_frame, size_t response_length,
                          compac_telemetry_t *telemetry);

/**
 * Parse status bits from raw register value (0x0100)
 * 
 * @param[in] status_word Raw 16-bit status register
 * @param[out] status Decoded status bits structure
 */
void compac_decode_status_bits(uint16_t status_word, compac_status_bits_t *status);

/**
 * Parse error flags from raw register value (0x0114)
 * 
 * @param[in] error_word Raw 16-bit error register
 * @param[out] errors Decoded error flags structure
 */
void compac_decode_error_flags(uint16_t error_word, compac_error_flags_t *errors);

/* ========================================================================
   Frame Construction (CNG-specific)
   ======================================================================== */

/**
 * Update unit price on dispenser (write to register 0x0201)
 * Constructs Modbus FC06 (write single) frame
 * 
 * @param[in] slave_address Dispenser slave ID (1-99)
 * @param[in] price_value Price in register units (e.g., 5000 = 50.00 with scale 0.01)
 * @param[out] frame_buffer Output frame buffer (minimum 8 bytes)
 * @param[out] frame_length Output frame length
 * @return COMPAC_BUILD_OK on success
 */
int compac_build_price_update_frame(uint8_t slave_address, uint16_t price_value,
                                    uint8_t *frame_buffer, size_t *frame_length);

/**
 * Lock or unlock dispenser (write to register 0x0200)
 * 
 * @param[in] slave_address Dispenser slave ID
 * @param[in] lock_state 0 = unlock, 1 = lock
 * @param[out] frame_buffer Output frame buffer (minimum 8 bytes)
 * @param[out] frame_length Output frame length
 * @return COMPAC_BUILD_OK on success
 */
int compac_build_lock_control_frame(uint8_t slave_address, uint8_t lock_state,
                                    uint8_t *frame_buffer, size_t *frame_length);

/**
 * Set target fill pressure (write to register 0x0202)
 * 
 * @param[in] slave_address Dispenser slave ID
 * @param[in] pressure_value Pressure in register units (e.g., 2500 = 25.00 bar with scale 0.01)
 * @param[out] frame_buffer Output frame buffer (minimum 8 bytes)
 * @param[out] frame_length Output frame length
 * @return COMPAC_BUILD_OK on success
 */
int compac_build_set_target_pressure_frame(uint8_t slave_address, uint16_t pressure_value,
                                          uint8_t *frame_buffer, size_t *frame_length);

/* ========================================================================
   CRC-16 Modbus Calculation
   ======================================================================== */

/**
 * Calculate CRC-16 for Modbus RTU (polynomial 0xA001, reflected)
 * 
 * @param[in] data Input data buffer
 * @param[in] length Length of data
 * @return 16-bit CRC value
 */
uint16_t compac_crc16_modbus(const uint8_t *data, size_t length);

/* ========================================================================
   Return Codes
   ======================================================================== */

#define COMPAC_PARSE_OK           0
#define COMPAC_PARSE_INVALID_CRC  1
#define COMPAC_PARSE_INVALID_FC   2
#define COMPAC_PARSE_SHORT_FRAME  3
#define COMPAC_PARSE_BCD_ERROR    4
#define COMPAC_PARSE_UNKNOWN      5

#define COMPAC_BUILD_OK           0
#define COMPAC_BUILD_BUFFER_SMALL 1

#endif // COMPAC_PROTOCOL_HANDLER_H
