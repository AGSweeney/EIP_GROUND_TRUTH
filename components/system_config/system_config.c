/*
 * Copyright (c) 2025, Adam G. Sweeney <agsweeney@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "system_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <string.h>

static const char *TAG = "system_config";
static const char *NVS_NAMESPACE = "system";
static const char *NVS_KEY_IPCONFIG = "ipconfig";
static const char *NVS_KEY_MODBUS_ENABLED = "modbus_enabled";
static const char *NVS_KEY_SENSOR_ENABLED = "sensor_enabled";
static const char *NVS_KEY_SENSOR_BYTE_OFFSET = "sens_byte_off";
static const char *NVS_KEY_MCP_ENABLED = "mcp_enabled";
static const char *NVS_KEY_MCP_DEVICE_TYPE = "mcp_dev_type";  // 0 = MCP23017, 1 = MCP23008
static const char *NVS_KEY_MCP_UPDATE_RATE_MS = "mcp_upd_rate";  // Update rate in milliseconds
static const char *NVS_KEY_MPU6050_ENABLED = "mpu6050_enabled";
static const char *NVS_KEY_MPU6050_BYTE_START = "mpu6050_byte";
static const char *NVS_KEY_LSM6DS3_ENABLED = "lsm6ds3_enabled";
static const char *NVS_KEY_LSM6DS3_BYTE_START = "lsm6ds3_byte";
static const char *NVS_KEY_TOOL_WEIGHT = "tool_weight";
static const char *NVS_KEY_TIP_FORCE = "tip_force";
static const char *NVS_KEY_CYLINDER_BORE = "cyl_bore";
static const char *NVS_KEY_I2C_INTERNAL_PULLUP = "i2c_pullup";
static const char *NVS_KEY_MPU6050_CAL_OFFSETS = "mpu6050_cal";

void system_ip_config_get_defaults(system_ip_config_t *config)
{
    if (config == NULL) {
        return;
    }
    
    memset(config, 0, sizeof(system_ip_config_t));
    config->use_dhcp = true;  // Default to DHCP
    // All other fields are 0 (DHCP will assign)
}

bool system_ip_config_load(system_ip_config_t *config)
{
    if (config == NULL) {
        return false;
    }
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No saved IP configuration found, using defaults");
            system_ip_config_get_defaults(config);
            return false;
        }
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }
    
    size_t required_size = sizeof(system_ip_config_t);
    err = nvs_get_blob(handle, NVS_KEY_IPCONFIG, config, &required_size);
    nvs_close(handle);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved IP configuration found, using defaults");
        system_ip_config_get_defaults(config);
        return false;
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load IP configuration: %s", esp_err_to_name(err));
        return false;
    }
    
    if (required_size != sizeof(system_ip_config_t)) {
        ESP_LOGW(TAG, "IP configuration size mismatch (expected %zu, got %zu), using defaults",
                 sizeof(system_ip_config_t), required_size);
        system_ip_config_get_defaults(config);
        return false;
    }
    
    ESP_LOGI(TAG, "IP configuration loaded successfully from NVS (DHCP=%s)", 
             config->use_dhcp ? "enabled" : "disabled");
    return true;
}

bool system_ip_config_save(const system_ip_config_t *config)
{
    if (config == NULL) {
        return false;
    }
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }
    
    err = nvs_set_blob(handle, NVS_KEY_IPCONFIG, config, sizeof(system_ip_config_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save IP configuration: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit IP configuration: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "IP configuration saved successfully to NVS");
    return true;
}

bool system_modbus_enabled_load(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No saved Modbus enabled state found, defaulting to disabled");
            return false;  // Default to disabled
        }
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;  // Default to disabled on error
    }
    
    uint8_t enabled = 0;  // Default to disabled
    size_t required_size = sizeof(uint8_t);
    err = nvs_get_blob(handle, NVS_KEY_MODBUS_ENABLED, &enabled, &required_size);
    nvs_close(handle);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved Modbus enabled state found, defaulting to disabled");
        return false;  // Default to disabled
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load Modbus enabled state: %s", esp_err_to_name(err));
        return false;  // Default to disabled on error
    }
    
    ESP_LOGI(TAG, "Modbus enabled state loaded from NVS: %s", enabled ? "enabled" : "disabled");
    return enabled != 0;
}

bool system_modbus_enabled_save(bool enabled)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }
    
    uint8_t enabled_val = enabled ? 1 : 0;
    err = nvs_set_blob(handle, NVS_KEY_MODBUS_ENABLED, &enabled_val, sizeof(uint8_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save Modbus enabled state: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit Modbus enabled state: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "Modbus enabled state saved successfully to NVS: %s", enabled ? "enabled" : "disabled");
    return true;
}

bool system_sensor_enabled_load(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No saved sensor enabled state found, defaulting to disabled");
            return false;  // Default to disabled
        }
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;  // Default to disabled on error
    }
    
    uint8_t enabled = 0;  // Default to disabled
    size_t required_size = sizeof(uint8_t);
    err = nvs_get_blob(handle, NVS_KEY_SENSOR_ENABLED, &enabled, &required_size);
    nvs_close(handle);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved sensor enabled state found, defaulting to disabled");
        return false;  // Default to disabled
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load sensor enabled state: %s", esp_err_to_name(err));
        return false;  // Default to disabled on error
    }
    
    ESP_LOGI(TAG, "Sensor enabled state loaded from NVS: %s", enabled ? "enabled" : "disabled");
    return enabled != 0;
}

bool system_sensor_enabled_save(bool enabled)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }
    
    uint8_t enabled_val = enabled ? 1 : 0;
    err = nvs_set_blob(handle, NVS_KEY_SENSOR_ENABLED, &enabled_val, sizeof(uint8_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save sensor enabled state: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit sensor enabled state: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "Sensor enabled state saved successfully to NVS: %s", enabled ? "enabled" : "disabled");
    return true;
}

uint8_t system_sensor_byte_offset_load(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No saved sensor byte offset found, defaulting to 0");
            return 0;  // Default to 0
        }
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return 0;  // Default to 0 on error
    }
    
    uint8_t start_byte = 0;  // Default to 0
    size_t required_size = sizeof(uint8_t);
    err = nvs_get_blob(handle, NVS_KEY_SENSOR_BYTE_OFFSET, &start_byte, &required_size);
    nvs_close(handle);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved sensor byte offset found, defaulting to 0");
        return 0;  // Default to 0
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load sensor byte offset: %s", esp_err_to_name(err));
        return 0;  // Default to 0 on error
    }
    
    // Validate: must be 0, 9, or 18
    if (start_byte != 0 && start_byte != 9 && start_byte != 18) {
        ESP_LOGW(TAG, "Invalid sensor byte offset %d found in NVS, defaulting to 0", start_byte);
        return 0;
    }
    
    ESP_LOGI(TAG, "Sensor byte offset loaded from NVS: %d (bytes %d-%d)", start_byte, start_byte, start_byte + 8);
    return start_byte;
}

bool system_sensor_byte_offset_save(uint8_t start_byte)
{
    // Validate: must be 0, 9, or 18
    if (start_byte != 0 && start_byte != 9 && start_byte != 18) {
        ESP_LOGE(TAG, "Invalid sensor byte offset: %d (must be 0, 9, or 18)", start_byte);
        return false;
    }
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }
    
    err = nvs_set_blob(handle, NVS_KEY_SENSOR_BYTE_OFFSET, &start_byte, sizeof(uint8_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save sensor byte offset: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit sensor byte offset: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "Sensor byte offset saved successfully to NVS: %d (bytes %d-%d)", start_byte, start_byte, start_byte + 8);
    return true;
}

bool system_mcp_enabled_load(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No saved MCP enabled state found, defaulting to disabled");
            return false;
        }
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }

    uint8_t enabled = 0;
    size_t required_size = sizeof(uint8_t);
    err = nvs_get_blob(handle, NVS_KEY_MCP_ENABLED, &enabled, &required_size);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved MCP enabled state found, defaulting to disabled");
        return false;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load MCP enabled state: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "MCP enabled state loaded from NVS: %s", enabled ? "enabled" : "disabled");
    return enabled != 0;
}

bool system_mcp_enabled_save(bool enabled)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }

    uint8_t enabled_val = enabled ? 1 : 0;
    err = nvs_set_blob(handle, NVS_KEY_MCP_ENABLED, &enabled_val, sizeof(uint8_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save MCP enabled state: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit MCP enabled state: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "MCP enabled state saved successfully to NVS: %s", enabled ? "enabled" : "disabled");
    return true;
}

uint8_t system_mcp_device_type_load(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No saved MCP device type found, defaulting to MCP23008");
            return 1;  // Default to MCP23008 (0 = MCP23017, 1 = MCP23008)
        }
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return 1;  // Default to MCP23008 on error
    }
    
    uint8_t device_type = 1;  // Default to MCP23008
    size_t required_size = sizeof(uint8_t);
    err = nvs_get_blob(handle, NVS_KEY_MCP_DEVICE_TYPE, &device_type, &required_size);
    nvs_close(handle);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved MCP device type found, defaulting to MCP23008");
        return 1;  // Default to MCP23008
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load MCP device type: %s", esp_err_to_name(err));
        return 1;  // Default to MCP23008 on error
    }
    
    // Validate: must be 0 (MCP23017) or 1 (MCP23008)
    if (device_type > 1) {
        ESP_LOGW(TAG, "Invalid MCP device type %d found in NVS, defaulting to MCP23008", device_type);
        return 1;
    }
    
    ESP_LOGI(TAG, "MCP device type loaded from NVS: %s", device_type == 0 ? "MCP23017" : "MCP23008");
    return device_type;
}

bool system_mcp_device_type_save(uint8_t device_type)
{
    // Validate: must be 0 (MCP23017) or 1 (MCP23008)
    if (device_type > 1) {
        ESP_LOGE(TAG, "Invalid MCP device type: %d (must be 0=MCP23017 or 1=MCP23008)", device_type);
        return false;
    }
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }
    
    err = nvs_set_blob(handle, NVS_KEY_MCP_DEVICE_TYPE, &device_type, sizeof(uint8_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save MCP device type: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit MCP device type: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "MCP device type saved successfully to NVS: %s", device_type == 0 ? "MCP23017" : "MCP23008");
    return true;
}

uint16_t system_mcp_update_rate_ms_load(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No saved MCP update rate found, defaulting to 20ms");
            return 20;  // Default to 20ms (50 Hz)
        }
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return 20;  // Default to 20ms on error
    }
    
    uint16_t update_rate_ms = 20;  // Default to 20ms
    size_t required_size = sizeof(uint16_t);
    err = nvs_get_blob(handle, NVS_KEY_MCP_UPDATE_RATE_MS, &update_rate_ms, &required_size);
    nvs_close(handle);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved MCP update rate found, defaulting to 20ms");
        return 20;  // Default to 20ms
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load MCP update rate: %s", esp_err_to_name(err));
        return 20;  // Default to 20ms on error
    }
    
    // Validate: reasonable range 10ms to 1000ms
    if (update_rate_ms < 10 || update_rate_ms > 1000) {
        ESP_LOGW(TAG, "Invalid MCP update rate %d ms found in NVS, defaulting to 20ms", update_rate_ms);
        return 20;
    }
    
    ESP_LOGI(TAG, "MCP update rate loaded from NVS: %d ms (%.1f Hz)", update_rate_ms, 1000.0f / update_rate_ms);
    return update_rate_ms;
}

bool system_mcp_update_rate_ms_save(uint16_t update_rate_ms)
{
    // Validate: reasonable range 10ms to 1000ms
    if (update_rate_ms < 10 || update_rate_ms > 1000) {
        ESP_LOGE(TAG, "Invalid MCP update rate %d ms (must be 10-1000ms)", update_rate_ms);
        return false;
    }
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }
    
    err = nvs_set_blob(handle, NVS_KEY_MCP_UPDATE_RATE_MS, &update_rate_ms, sizeof(uint16_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save MCP update rate: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit MCP update rate: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "MCP update rate saved successfully to NVS: %d ms (%.1f Hz)", update_rate_ms, 1000.0f / update_rate_ms);
    return true;
}

bool system_mpu6050_enabled_load(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No saved MPU6050 enabled state found, defaulting to disabled");
            return false;  // Default to disabled
        }
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;  // Default to disabled on error
    }
    
    uint8_t enabled = 0;  // Default to disabled
    size_t required_size = sizeof(uint8_t);
    err = nvs_get_blob(handle, NVS_KEY_MPU6050_ENABLED, &enabled, &required_size);
    nvs_close(handle);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved MPU6050 enabled state found, defaulting to disabled");
        return false;  // Default to disabled
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load MPU6050 enabled state: %s", esp_err_to_name(err));
        return false;  // Default to disabled on error
    }
    
    // Only log on first load or when explicitly needed (reduce log spam)
    // ESP_LOGI(TAG, "MPU6050 enabled state loaded from NVS: %s", enabled ? "enabled" : "disabled");
    return enabled != 0;
}

bool system_mpu6050_enabled_save(bool enabled)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }
    
    uint8_t enabled_val = enabled ? 1 : 0;
    err = nvs_set_blob(handle, NVS_KEY_MPU6050_ENABLED, &enabled_val, sizeof(uint8_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save MPU6050 enabled state: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit MPU6050 enabled state: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "MPU6050 enabled state saved successfully to NVS: %s", enabled ? "enabled" : "disabled");
    return true;
}

uint8_t system_mpu6050_byte_start_load(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No saved MPU6050 byte start found, defaulting to 0");
            return 0;  // Default to 0 (bytes 0-19 for roll, pitch, ground_angle, bottom_pressure, top_pressure)
        }
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return 0;  // Default to 0 on error
    }
    
    uint8_t byte_start = 0;  // Default to 0
    size_t required_size = sizeof(uint8_t);
    err = nvs_get_blob(handle, NVS_KEY_MPU6050_BYTE_START, &byte_start, &required_size);
    nvs_close(handle);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved MPU6050 byte start found, defaulting to 0");
        return 0;  // Default to 0
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load MPU6050 byte start: %s", esp_err_to_name(err));
        return 0;  // Default to 0 on error
    }
    
    // Validate: must be 0-12 (MPU6050 uses 20 bytes: 5 int32_t for roll, pitch, ground_angle, bottom_pressure, top_pressure)
    // Values are stored as scaled integers: degrees * 10000, pressure * 1000
    if (byte_start > 12) {
        ESP_LOGW(TAG, "Invalid MPU6050 byte start %d found in NVS (max 12, uses 20 bytes), defaulting to 0", byte_start);
        return 0;
    }
    
    ESP_LOGI(TAG, "MPU6050 byte start loaded from NVS: %d (uses 20 bytes: %d-%d for roll, pitch, ground_angle, bottom_pressure, top_pressure)", 
             byte_start, byte_start, byte_start + 19);
    return byte_start;
}

bool system_mpu6050_byte_start_save(uint8_t byte_start)
{
    // Validate: MPU6050 uses 20 bytes (5 int32_t: roll, pitch, ground_angle, bottom_pressure, top_pressure)
    // Values are stored as scaled integers: degrees * 10000, pressure * 1000
    if (byte_start > 12) {
        ESP_LOGE(TAG, "Invalid MPU6050 byte start %d (max 12, uses 20 bytes)", byte_start);
        return false;
    }
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }
    
    err = nvs_set_blob(handle, NVS_KEY_MPU6050_BYTE_START, &byte_start, sizeof(uint8_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save MPU6050 byte start: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit MPU6050 byte start: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "MPU6050 byte start saved to NVS: %d", byte_start);
    return true;
}

bool system_lsm6ds3_enabled_load(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No saved LSM6DS3 enabled state found, defaulting to disabled");
            return false;  // Default to disabled
        }
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;  // Default to disabled on error
    }
    
    uint8_t enabled = 0;  // Default to disabled
    size_t required_size = sizeof(uint8_t);
    err = nvs_get_blob(handle, NVS_KEY_LSM6DS3_ENABLED, &enabled, &required_size);
    nvs_close(handle);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved LSM6DS3 enabled state found, defaulting to disabled");
        return false;  // Default to disabled
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load LSM6DS3 enabled state: %s", esp_err_to_name(err));
        return false;  // Default to disabled on error
    }
    
    return enabled != 0;
}

bool system_lsm6ds3_enabled_save(bool enabled)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }
    
    uint8_t enabled_val = enabled ? 1 : 0;
    err = nvs_set_blob(handle, NVS_KEY_LSM6DS3_ENABLED, &enabled_val, sizeof(uint8_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save LSM6DS3 enabled state: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit LSM6DS3 enabled state: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "LSM6DS3 enabled state saved successfully to NVS: %s", enabled ? "enabled" : "disabled");
    return true;
}

uint8_t system_lsm6ds3_byte_start_load(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No saved LSM6DS3 byte start found, defaulting to 0");
            return 0;  // Default to 0 (bytes 0-19 for roll, pitch, ground_angle, bottom_pressure, top_pressure)
        }
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return 0;  // Default to 0 on error
    }
    
    uint8_t byte_start = 0;  // Default to 0
    size_t required_size = sizeof(uint8_t);
    err = nvs_get_blob(handle, NVS_KEY_LSM6DS3_BYTE_START, &byte_start, &required_size);
    nvs_close(handle);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved LSM6DS3 byte start found, defaulting to 0");
        return 0;  // Default to 0
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load LSM6DS3 byte start: %s", esp_err_to_name(err));
        return 0;  // Default to 0 on error
    }
    
    // Validate: must be 0-12 (LSM6DS3 uses 20 bytes: 5 int32_t for roll, pitch, ground_angle, bottom_pressure, top_pressure)
    // Values are stored as scaled integers: degrees * 10000, pressure * 1000
    if (byte_start > 12) {
        ESP_LOGW(TAG, "Invalid LSM6DS3 byte start %d found in NVS (max 12, uses 20 bytes), defaulting to 0", byte_start);
        return 0;
    }
    
    // LSM6DS3 byte start loaded silently (no console logging)
    return byte_start;
}

bool system_lsm6ds3_byte_start_save(uint8_t byte_start)
{
    // Validate: LSM6DS3 uses 20 bytes (5 int32_t: roll, pitch, ground_angle, bottom_pressure, top_pressure)
    // Values are stored as scaled integers: degrees * 10000, pressure * 1000
    if (byte_start > 12) {
        ESP_LOGE(TAG, "Invalid LSM6DS3 byte start %d (max 12, uses 20 bytes)", byte_start);
        return false;
    }
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }
    
    err = nvs_set_blob(handle, NVS_KEY_LSM6DS3_BYTE_START, &byte_start, sizeof(uint8_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save LSM6DS3 byte start: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit LSM6DS3 byte start: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "LSM6DS3 byte start saved to NVS: %d", byte_start);
    return true;
}

uint8_t system_tool_weight_load(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            return 50;  // Default to 50 lbs
        }
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return 50;  // Default to 50 lbs on error
    }
    
    uint8_t tool_weight = 50;  // Default to 50 lbs
    size_t required_size = sizeof(uint8_t);
    err = nvs_get_blob(handle, NVS_KEY_TOOL_WEIGHT, &tool_weight, &required_size);
    nvs_close(handle);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return 50;  // Default to 50 lbs
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load tool weight: %s", esp_err_to_name(err));
        return 50;  // Default to 50 lbs on error
    }
    
    return tool_weight;
}

bool system_tool_weight_save(uint8_t tool_weight)
{
    if (tool_weight == 0) {
        ESP_LOGE(TAG, "Invalid tool weight %d (must be 1-255 lbs)", tool_weight);
        return false;
    }
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }
    
    err = nvs_set_blob(handle, NVS_KEY_TOOL_WEIGHT, &tool_weight, sizeof(uint8_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save tool weight: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit tool weight: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "Tool weight saved successfully to NVS: %d lbs", tool_weight);
    return true;
}

uint8_t system_tip_force_load(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            return 20;  // Default to 20 lbs
        }
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return 20;  // Default to 20 lbs on error
    }
    
    uint8_t tip_force = 20;  // Default to 20 lbs
    size_t required_size = sizeof(uint8_t);
    err = nvs_get_blob(handle, NVS_KEY_TIP_FORCE, &tip_force, &required_size);
    nvs_close(handle);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return 20;  // Default to 20 lbs
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load tip force: %s", esp_err_to_name(err));
        return 20;  // Default to 20 lbs on error
    }
    
    return tip_force;
}

bool system_tip_force_save(uint8_t tip_force)
{
    if (tip_force == 0) {
        ESP_LOGE(TAG, "Invalid tip force %d (must be 1-255 lbs)", tip_force);
        return false;
    }
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }
    
    err = nvs_set_blob(handle, NVS_KEY_TIP_FORCE, &tip_force, sizeof(uint8_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save tip force: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit tip force: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "Tip force saved successfully to NVS: %d lbs", tip_force);
    return true;
}

float system_cylinder_bore_load(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            return 1.0f;  // Default to 1.0 inch
        }
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return 1.0f;  // Default to 1.0 inch on error
    }
    
    float cylinder_bore = 1.0f;  // Default to 1.0 inch
    size_t required_size = sizeof(float);
    err = nvs_get_blob(handle, NVS_KEY_CYLINDER_BORE, &cylinder_bore, &required_size);
    nvs_close(handle);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return 1.0f;  // Default to 1.0 inch
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load cylinder bore: %s", esp_err_to_name(err));
        return 1.0f;  // Default to 1.0 inch on error
    }
    
    return cylinder_bore;
}

bool system_cylinder_bore_save(float cylinder_bore)
{
    if (cylinder_bore <= 0.0f || cylinder_bore > 10.0f) {
        ESP_LOGE(TAG, "Invalid cylinder bore %.2f (must be between 0.1 and 10.0 inches)", cylinder_bore);
        return false;
    }
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }
    
    err = nvs_set_blob(handle, NVS_KEY_CYLINDER_BORE, &cylinder_bore, sizeof(float));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save cylinder bore: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit cylinder bore: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "Cylinder bore saved successfully to NVS: %.2f inches", cylinder_bore);
    return true;
}

bool system_i2c_internal_pullup_load(void)
{
    // Get compile-time default (Kconfig option)
    #ifdef CONFIG_OPENER_I2C_INTERNAL_PULLUP
    bool default_enabled = CONFIG_OPENER_I2C_INTERNAL_PULLUP;
    #else
    bool default_enabled = false;  // Default to disabled if not defined
    #endif
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No saved I2C pull-up setting found, using compile-time default");
            return default_enabled;
        }
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return default_enabled;
    }
    
    uint8_t enabled = default_enabled ? 1 : 0;  // Default to Kconfig value
    size_t required_size = sizeof(uint8_t);
    err = nvs_get_blob(handle, NVS_KEY_I2C_INTERNAL_PULLUP, &enabled, &required_size);
    nvs_close(handle);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved I2C pull-up setting found, using compile-time default");
        return default_enabled;
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load I2C pull-up setting: %s", esp_err_to_name(err));
        return default_enabled;
    }
    
    ESP_LOGI(TAG, "I2C internal pull-up setting loaded from NVS: %s", enabled ? "enabled" : "disabled");
    return enabled != 0;
}

bool system_i2c_internal_pullup_save(bool enabled)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }
    
    uint8_t value = enabled ? 1 : 0;
    err = nvs_set_blob(handle, NVS_KEY_I2C_INTERNAL_PULLUP, &value, sizeof(uint8_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save I2C pull-up setting: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit I2C pull-up setting: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "I2C internal pull-up setting saved to NVS: %s", enabled ? "enabled" : "disabled");
    return true;
}

// MPU6050 calibration offsets structure (6 int16_t values: accel X, Y, Z, gyro X, Y, Z)
typedef struct {
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_z;
} mpu6050_cal_offsets_t;

bool system_mpu6050_cal_offsets_load(int16_t *accel_x, int16_t *accel_y, int16_t *accel_z,
                                      int16_t *gyro_x, int16_t *gyro_y, int16_t *gyro_z)
{
    if (!accel_x || !accel_y || !accel_z || !gyro_x || !gyro_y || !gyro_z) {
        return false;
    }
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            // No calibration stored, return zeros
            *accel_x = *accel_y = *accel_z = 0;
            *gyro_x = *gyro_y = *gyro_z = 0;
            return false;
        }
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }
    
    mpu6050_cal_offsets_t offsets = {0};
    size_t required_size = sizeof(mpu6050_cal_offsets_t);
    err = nvs_get_blob(handle, NVS_KEY_MPU6050_CAL_OFFSETS, &offsets, &required_size);
    nvs_close(handle);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *accel_x = *accel_y = *accel_z = 0;
        *gyro_x = *gyro_y = *gyro_z = 0;
        return false;
    }
    
    if (err != ESP_OK || required_size != sizeof(mpu6050_cal_offsets_t)) {
        ESP_LOGE(TAG, "Failed to load MPU6050 calibration offsets: %s", esp_err_to_name(err));
        return false;
    }
    
    *accel_x = offsets.accel_x;
    *accel_y = offsets.accel_y;
    *accel_z = offsets.accel_z;
    *gyro_x = offsets.gyro_x;
    *gyro_y = offsets.gyro_y;
    *gyro_z = offsets.gyro_z;
    
    ESP_LOGI(TAG, "MPU6050 calibration offsets loaded from NVS");
    return true;
}

bool system_mpu6050_cal_offsets_save(int16_t accel_x, int16_t accel_y, int16_t accel_z,
                                     int16_t gyro_x, int16_t gyro_y, int16_t gyro_z)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }
    
    mpu6050_cal_offsets_t offsets = {
        .accel_x = accel_x,
        .accel_y = accel_y,
        .accel_z = accel_z,
        .gyro_x = gyro_x,
        .gyro_y = gyro_y,
        .gyro_z = gyro_z
    };
    
    err = nvs_set_blob(handle, NVS_KEY_MPU6050_CAL_OFFSETS, &offsets, sizeof(mpu6050_cal_offsets_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save MPU6050 calibration offsets: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit MPU6050 calibration offsets: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "MPU6050 calibration offsets saved to NVS");
    return true;
}

