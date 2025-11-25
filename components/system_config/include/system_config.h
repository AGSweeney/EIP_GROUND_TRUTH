#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include "lwip/ip4_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief IP configuration structure
 */
typedef struct {
    bool use_dhcp;              // true for DHCP, false for static
    uint32_t ip_address;        // IP address in network byte order
    uint32_t netmask;           // Network mask in network byte order
    uint32_t gateway;           // Gateway in network byte order
    uint32_t dns1;              // Primary DNS in network byte order
    uint32_t dns2;              // Secondary DNS in network byte order
} system_ip_config_t;

/**
 * @brief Get default IP configuration (DHCP enabled)
 */
void system_ip_config_get_defaults(system_ip_config_t *config);

/**
 * @brief Load IP configuration from NVS
 * @param config Pointer to config structure to fill
 * @return true if loaded successfully, false if using defaults
 */
bool system_ip_config_load(system_ip_config_t *config);

/**
 * @brief Save IP configuration to NVS
 * @param config Pointer to config structure to save
 * @return true on success, false on error
 */
bool system_ip_config_save(const system_ip_config_t *config);

/**
 * @brief Load Modbus enabled state from NVS
 * @return true if Modbus is enabled, false if disabled or not set
 */
bool system_modbus_enabled_load(void);

/**
 * @brief Save Modbus enabled state to NVS
 * @param enabled true to enable Modbus, false to disable
 * @return true on success, false on error
 */
bool system_modbus_enabled_save(bool enabled);

/**
 * @brief Load VL53L1x sensor enabled state from NVS
 * @return true if sensor is enabled, false if disabled or not set
 */
bool system_sensor_enabled_load(void);

/**
 * @brief Save VL53L1x sensor enabled state to NVS
 * @param enabled true to enable sensor, false to disable
 * @return true on success, false on error
 */
bool system_sensor_enabled_save(bool enabled);

/**
 * @brief Load VL53L1x sensor data start byte offset from NVS
 * @return Start byte offset (0, 9, or 18). Defaults to 0 if not set or invalid.
 */
uint8_t system_sensor_byte_offset_load(void);

/**
 * @brief Save VL53L1x sensor data start byte offset to NVS
 * @param start_byte Start byte offset (must be 0, 9, or 18)
 * @return true on success, false on error or invalid value
 */
bool system_sensor_byte_offset_save(uint8_t start_byte);

/**
 * @brief Load MCP enabled state from NVS
 * @return true if MCP is enabled, false if disabled or not set
 */
bool system_mcp_enabled_load(void);

/**
 * @brief Save MCP enabled state to NVS
 * @param enabled true to enable MCP, false to disable
 * @return true on success, false on error
 */
bool system_mcp_enabled_save(bool enabled);

/**
 * @brief Load MCP device type preference from NVS
 * @return 0 for MCP23017, 1 for MCP23008. Defaults to 1 (MCP23008) if not set or invalid.
 */
uint8_t system_mcp_device_type_load(void);

/**
 * @brief Save MCP device type preference to NVS
 * @param device_type 0 for MCP23017, 1 for MCP23008
 * @return true on success, false on error or invalid value
 */
bool system_mcp_device_type_save(uint8_t device_type);

/**
 * @brief Load MCP I/O task update rate from NVS
 * @return Update rate in milliseconds. Defaults to 20ms (50 Hz) if not set or invalid.
 */
uint16_t system_mcp_update_rate_ms_load(void);

/**
 * @brief Save MCP I/O task update rate to NVS
 * @param update_rate_ms Update rate in milliseconds (10-1000ms)
 * @return true on success, false on error or invalid value
 */
bool system_mcp_update_rate_ms_save(uint16_t update_rate_ms);

/**
 * @brief Load MPU6050 enabled state from NVS
 * @return true if MPU6050 is enabled, false if disabled or not set
 */
bool system_mpu6050_enabled_load(void);

/**
 * @brief Save MPU6050 enabled state to NVS
 * @param enabled true to enable MPU6050, false to disable
 * @return true on success, false on error
 */
bool system_mpu6050_enabled_save(bool enabled);

/**
 * @brief Load MPU6050 input byte start from NVS
 * @return byte start position (default 0 if not set)
 */
uint8_t system_mpu6050_byte_start_load(void);

/**
 * @brief Save MPU6050 input byte start to NVS
 * @param byte_start starting byte position in Input Assembly (0-12, uses 20 bytes: roll, pitch, ground_angle, bottom_pressure, top_pressure)
 * @return true on success, false on error
 */
bool system_mpu6050_byte_start_save(uint8_t byte_start);

/**
 * @brief Load LSM6DS3 enabled state from NVS
 * @return true if LSM6DS3 is enabled, false if disabled or not set
 */
bool system_lsm6ds3_enabled_load(void);

/**
 * @brief Save LSM6DS3 enabled state to NVS
 * @param enabled true to enable LSM6DS3, false to disable
 * @return true on success, false on error
 */
bool system_lsm6ds3_enabled_save(bool enabled);

/**
 * @brief Load LSM6DS3 input byte start from NVS
 * @return byte start position (default 0 if not set)
 */
uint8_t system_lsm6ds3_byte_start_load(void);

/**
 * @brief Save LSM6DS3 input byte start to NVS
 * @param byte_start starting byte position in Input Assembly (0-12, uses 20 bytes: roll, pitch, ground_angle, bottom_pressure, top_pressure)
 * @return true on success, false on error
 */
bool system_lsm6ds3_byte_start_save(uint8_t byte_start);

/**
 * @brief Load tool weight from NVS
 * @return Tool weight in lbs (defaults to 50 if not set)
 */
uint8_t system_tool_weight_load(void);

/**
 * @brief Save tool weight to NVS
 * @param tool_weight Tool weight in lbs (1-255)
 * @return true on success, false on error
 */
bool system_tool_weight_save(uint8_t tool_weight);

/**
 * @brief Load tip force from NVS
 * @return Tip force in lbs (defaults to 20 if not set)
 */
uint8_t system_tip_force_load(void);

/**
 * @brief Save tip force to NVS
 * @param tip_force Tip force in lbs (1-255)
 * @return true on success, false on error
 */
bool system_tip_force_save(uint8_t tip_force);

/**
 * @brief Load cylinder bore size from NVS
 * @return Cylinder bore size in inches (defaults to 1.0 if not set)
 */
float system_cylinder_bore_load(void);

/**
 * @brief Save cylinder bore size to NVS
 * @param cylinder_bore Cylinder bore size in inches (0.1-10.0)
 * @return true on success, false on error
 */
bool system_cylinder_bore_save(float cylinder_bore);

/**
 * @brief Load I2C internal pull-up setting from NVS
 * @return true if internal pull-ups are enabled, false if disabled. Falls back to CONFIG_OPENER_I2C_INTERNAL_PULLUP if not set.
 */
bool system_i2c_internal_pullup_load(void);

/**
 * @brief Save I2C internal pull-up setting to NVS
 * @param enabled true to enable internal pull-ups, false to disable (use external)
 * @return true on success, false on error
 * @note Changes take effect on next boot (I2C buses are initialized at boot time)
 */
bool system_i2c_internal_pullup_save(bool enabled);

/**
 * @brief Load MPU6050 calibration offsets from NVS
 * @param accel_x Pointer to store accelerometer X offset
 * @param accel_y Pointer to store accelerometer Y offset
 * @param accel_z Pointer to store accelerometer Z offset
 * @param gyro_x Pointer to store gyroscope X offset
 * @param gyro_y Pointer to store gyroscope Y offset
 * @param gyro_z Pointer to store gyroscope Z offset
 * @return true if offsets were loaded, false if not found or error (offsets set to 0)
 */
bool system_mpu6050_cal_offsets_load(int16_t *accel_x, int16_t *accel_y, int16_t *accel_z,
                                     int16_t *gyro_x, int16_t *gyro_y, int16_t *gyro_z);

/**
 * @brief Save MPU6050 calibration offsets to NVS
 * @param accel_x Accelerometer X offset
 * @param accel_y Accelerometer Y offset
 * @param accel_z Accelerometer Z offset
 * @param gyro_x Gyroscope X offset
 * @param gyro_y Gyroscope Y offset
 * @param gyro_z Gyroscope Z offset
 * @return true on success, false on error
 */
bool system_mpu6050_cal_offsets_save(int16_t accel_x, int16_t accel_y, int16_t accel_z,
                                     int16_t gyro_x, int16_t gyro_y, int16_t gyro_z);

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_CONFIG_H

