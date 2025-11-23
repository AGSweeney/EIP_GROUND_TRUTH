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

#include "webui_api.h"
#include "ota_manager.h"
#include "system_config.h"
#include "driver/i2c_master.h"
#include "modbus_tcp.h"
#include "ciptcpipinterface.h"
#include "nvtcpip.h"
#include "log_buffer.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Forward declarations for assembly access
extern uint8_t g_assembly_data064[32];
extern uint8_t g_assembly_data096[32];
extern uint8_t g_assembly_data097[10];

// Forward declaration for MPU6050 enabled state setter
extern void sample_application_set_mpu6050_enabled(bool enabled);


// Forward declaration for assembly mutex access
extern SemaphoreHandle_t sample_application_get_assembly_mutex(void);

static const char *TAG = "webui_api";

// Cache for MPU6050 enabled state and byte offset to avoid frequent NVS reads
static bool s_cached_mpu6050_enabled = false;
static bool s_mpu6050_enabled_cached = false;
static uint8_t s_cached_mpu6050_byte_start = 0;
static bool s_mpu6050_byte_start_cached = false;

// Cache for Modbus enabled state to avoid frequent NVS reads
static bool s_cached_modbus_enabled = false;
static bool s_modbus_enabled_cached = false;

// Helper function to send JSON response
static esp_err_t send_json_response(httpd_req_t *req, cJSON *json, esp_err_t status_code)
{
    char *json_str = cJSON_Print(json);
    if (json_str == NULL) {
        cJSON_Delete(json);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, status_code == ESP_OK ? "200 OK" : "400 Bad Request");
    httpd_resp_send(req, json_str, strlen(json_str));
    
    free(json_str);
    cJSON_Delete(json);
    return ESP_OK;
}

// Helper function to send JSON error response
static esp_err_t send_json_error(httpd_req_t *req, const char *message, int http_status)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "status", "error");
    cJSON_AddStringToObject(json, "message", message);
    
    char *json_str = cJSON_Print(json);
    if (json_str == NULL) {
        cJSON_Delete(json);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    if (http_status == 400) {
        httpd_resp_set_status(req, "400 Bad Request");
    } else if (http_status == 500) {
        httpd_resp_set_status(req, "500 Internal Server Error");
    } else {
        httpd_resp_set_status(req, "400 Bad Request");
    }
    httpd_resp_send(req, json_str, strlen(json_str));
    
    free(json_str);
    cJSON_Delete(json);
    return ESP_OK;
}

// POST /api/reboot - Reboot the device
static esp_err_t api_reboot_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Reboot requested via web UI");
    
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "message", "Device rebooting...");
    
    esp_err_t ret = send_json_response(req, response, ESP_OK);
    
    // Give a small delay to ensure response is sent
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Reboot the device
    esp_restart();
    
    return ret; // This will never be reached
}

// POST /api/ota/update - Trigger OTA update (supports both URL and file upload)
static esp_err_t api_ota_update_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "OTA update request received");
    
    // Check content type
    char content_type[256];
    esp_err_t ret = httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Missing Content-Type header (ret=%d)", ret);
        return send_json_error(req, "Missing Content-Type", 400);
    }
    
    ESP_LOGI(TAG, "OTA update request, Content-Type: %s", content_type);
    
    // Handle file upload (multipart/form-data) - Use streaming to avoid memory issues
    if (strstr(content_type, "multipart/form-data") != NULL) {
        // Get content length - may be 0 if chunked transfer
        size_t content_len = req->content_len;
        ESP_LOGI(TAG, "Content-Length: %d", content_len);
        
        // Validate size
        if (content_len > 2 * 1024 * 1024) { // Max 2MB for safety
            ESP_LOGW(TAG, "Content length too large: %d", content_len);
            return send_json_error(req, "File too large (max 2MB)", 400);
        }
        
        // Parse multipart boundary first
        const char *boundary_str = strstr(content_type, "boundary=");
        if (boundary_str == NULL) {
            ESP_LOGW(TAG, "No boundary found in Content-Type");
            return send_json_error(req, "Invalid multipart data: no boundary", 400);
        }
        boundary_str += 9; // Skip "boundary="
        
        // Extract boundary value
        char boundary[128];
        int boundary_len = 0;
        while (*boundary_str && *boundary_str != ';' && *boundary_str != ' ' && *boundary_str != '\r' && *boundary_str != '\n' && boundary_len < 127) {
            boundary[boundary_len++] = *boundary_str++;
        }
        boundary[boundary_len] = '\0';
        ESP_LOGI(TAG, "Multipart boundary: %s", boundary);
        
        // Use a small buffer to read multipart headers (64KB should be enough)
        const size_t header_buffer_size = 64 * 1024;
        char *header_buffer = malloc(header_buffer_size);
        if (header_buffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for header buffer");
            return send_json_error(req, "Failed to allocate memory", 500);
        }
        
        // Read enough to find the data separator (\r\n\r\n)
        size_t header_read = 0;
        bool found_separator = false;
        while (header_read < header_buffer_size - 1) {
            int ret = httpd_req_recv(req, header_buffer + header_read, header_buffer_size - header_read - 1);
            if (ret <= 0) {
                if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                    continue;
                }
                ESP_LOGE(TAG, "Error reading headers: %d", ret);
                free(header_buffer);
                return send_json_error(req, "Failed to read request headers", 500);
            }
            header_read += ret;
            header_buffer[header_read] = '\0'; // Null terminate for string search
            
            // Look for data separator
            if (strstr(header_buffer, "\r\n\r\n") != NULL || strstr(header_buffer, "\n\n") != NULL) {
                found_separator = true;
                break;
            }
        }
        
        if (!found_separator) {
            ESP_LOGW(TAG, "Could not find data separator in multipart headers");
            free(header_buffer);
            return send_json_error(req, "Invalid multipart format: no data separator", 400);
        }
        
        // Find where data starts
        char *data_start = strstr(header_buffer, "\r\n\r\n");
        size_t header_len = 0;
        if (data_start != NULL) {
            header_len = (data_start - header_buffer) + 4;
        } else {
            data_start = strstr(header_buffer, "\n\n");
            if (data_start != NULL) {
                header_len = (data_start - header_buffer) + 2;
            } else {
                free(header_buffer);
                return send_json_error(req, "Invalid multipart format", 400);
            }
        }
        
        // Calculate how much data we already have in the buffer
        size_t data_in_buffer = header_read - header_len;
        
        // Start streaming OTA update
        // Estimate firmware size: Content-Length minus multipart headers (typically ~1KB)
        // This gives us a reasonable estimate for progress tracking
        size_t estimated_firmware_size = 0;
        if (content_len > 0) {
            // Subtract estimated multipart header overhead (boundary + headers ~1KB)
            estimated_firmware_size = (content_len > 1024) ? (content_len - 1024) : content_len;
        }
        esp_ota_handle_t ota_handle = ota_manager_start_streaming_update(estimated_firmware_size);
        if (ota_handle == 0) {
            ESP_LOGE(TAG, "Failed to start streaming OTA update - check serial logs for details");
            free(header_buffer);
            return send_json_error(req, "Failed to start OTA update. Check device logs for details.", 500);
        }
        
        // Prepare boundary strings for detection
        char start_boundary[256];
        char end_boundary[256];
        snprintf(start_boundary, sizeof(start_boundary), "--%s", boundary);
        snprintf(end_boundary, sizeof(end_boundary), "--%s--", boundary);
        
        bool done = false;
        
        // Write data we already have in buffer (check for boundary first)
        if (data_in_buffer > 0) {
            // Check if boundary is already in the initial data
            char *boundary_in_header = strstr((char *)(header_buffer + header_len), start_boundary);
            
            if (boundary_in_header != NULL) {
                // Boundary found in initial data - only write up to it
                size_t initial_to_write = boundary_in_header - (header_buffer + header_len);
                // Remove trailing \r\n
                while (initial_to_write > 0 && 
                       (header_buffer[header_len + initial_to_write - 1] == '\r' || 
                        header_buffer[header_len + initial_to_write - 1] == '\n')) {
                    initial_to_write--;
                }
                if (initial_to_write > 0) {
                    if (!ota_manager_write_streaming_chunk(ota_handle, (const uint8_t *)(header_buffer + header_len), initial_to_write)) {
                        ESP_LOGE(TAG, "Failed to write initial chunk");
                        free(header_buffer);
                        return send_json_error(req, "Failed to write firmware data", 500);
                    }
                    data_in_buffer = initial_to_write;
                } else {
                    data_in_buffer = 0;  // No data to write, boundary was at start
                }
                done = true;  // We're done, boundary found
            } else {
                // No boundary in initial data, write it all
                if (!ota_manager_write_streaming_chunk(ota_handle, (const uint8_t *)(header_buffer + header_len), data_in_buffer)) {
                    ESP_LOGE(TAG, "Failed to write initial chunk");
                    free(header_buffer);
                    return send_json_error(req, "Failed to write firmware data", 500);
                }
            }
        }
        
        free(header_buffer); // Free header buffer, we'll use a smaller chunk buffer now
        
        // If boundary was found in initial data, we're done
        if (done) {
            ESP_LOGI(TAG, "Streamed %d bytes to OTA partition", data_in_buffer);
            
            // Finish OTA update
            cJSON *response = cJSON_CreateObject();
            cJSON_AddStringToObject(response, "status", "ok");
            cJSON_AddStringToObject(response, "message", "Firmware uploaded successfully. Finishing update and rebooting...");
            
            esp_err_t resp_err = send_json_response(req, response, ESP_OK);
            vTaskDelay(pdMS_TO_TICKS(100));
            
            if (!ota_manager_finish_streaming_update(ota_handle)) {
                ESP_LOGE(TAG, "Failed to finish streaming OTA update");
                return ESP_FAIL;
            }
            return resp_err;
        }
        
        // Stream remaining data in chunks (64KB chunks)
        const size_t chunk_size = 64 * 1024;
        char *chunk_buffer = malloc(chunk_size);
        if (chunk_buffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate chunk buffer");
            return send_json_error(req, "Failed to allocate memory", 500);
        }
        
        size_t total_written = data_in_buffer;
        
        while (!done) {
            int ret = httpd_req_recv(req, chunk_buffer, chunk_size);
            if (ret <= 0) {
                if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                    continue;
                }
                // Connection closed or error
                done = true;
                break;
            }
            
            // Search for boundary markers in this chunk
            // Look for both start boundary (--boundary) and end boundary (--boundary--)
            char *boundary_pos = NULL;
            size_t to_write = ret;
            bool found_end = false;
            
            // First check for end boundary (--boundary--)
            char *search_start = chunk_buffer;
            while ((search_start = strstr(search_start, end_boundary)) != NULL) {
                // Check if this is actually a boundary (should be preceded by \r\n or at start)
                if (search_start == chunk_buffer || 
                    (search_start > chunk_buffer && 
                     (search_start[-1] == '\n' || (search_start[-1] == '\r' && search_start > chunk_buffer + 1 && search_start[-2] == '\n')))) {
                    boundary_pos = search_start;
                    found_end = true;
                    break;
                }
                search_start++;
            }
            
            // If not found, check for start boundary (--boundary) - this indicates next part
            if (boundary_pos == NULL) {
                search_start = chunk_buffer;
                while ((search_start = strstr(search_start, start_boundary)) != NULL) {
                    // Check if this is actually a boundary (should be preceded by \r\n or at start)
                    if (search_start == chunk_buffer || 
                        (search_start > chunk_buffer && 
                         (search_start[-1] == '\n' || (search_start[-1] == '\r' && search_start > chunk_buffer + 1 && search_start[-2] == '\n')))) {
                        // Make sure it's not the end boundary (which is longer)
                        size_t end_boundary_len = strlen(end_boundary);
                        if (strncmp(search_start, end_boundary, end_boundary_len) != 0) {
                            boundary_pos = search_start;
                            break;
                        }
                    }
                    search_start++;
                }
            }
            
            if (boundary_pos != NULL) {
                // Found boundary - only write up to it (excluding the boundary itself and leading \r\n)
                to_write = boundary_pos - chunk_buffer;
                
                // Remove any trailing \r\n before the boundary (multipart boundaries are preceded by \r\n)
                while (to_write > 0 && (chunk_buffer[to_write - 1] == '\r' || chunk_buffer[to_write - 1] == '\n')) {
                    to_write--;
                }
                
                // Also check if there's a \r\n sequence before boundary_pos that we should exclude
                if (to_write >= 2 && chunk_buffer[to_write - 2] == '\r' && chunk_buffer[to_write - 1] == '\n') {
                    to_write -= 2;
                } else if (to_write >= 1 && chunk_buffer[to_write - 1] == '\n') {
                    to_write -= 1;
                }
                
                done = true;
            }
            
            if (to_write > 0) {
                if (!ota_manager_write_streaming_chunk(ota_handle, (const uint8_t *)chunk_buffer, to_write)) {
                    ESP_LOGE(TAG, "Failed to write chunk at offset %d", total_written);
                    free(chunk_buffer);
                    return send_json_error(req, "Failed to write firmware data", 500);
                }
                total_written += to_write;
            }
            
            // If we found the end boundary, we're done
            if (found_end) {
                done = true;
            }
        }
        
        free(chunk_buffer);
        
        ESP_LOGI(TAG, "Streamed %d bytes to OTA partition", total_written);
        
        // Finish OTA update (this will set boot partition and reboot)
        // Send HTTP response BEFORE finishing, as the device will reboot
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "status", "ok");
        cJSON_AddStringToObject(response, "message", "Firmware uploaded successfully. Finishing update and rebooting...");
        
        // Send response first
        esp_err_t resp_err = send_json_response(req, response, ESP_OK);
        
        // Small delay to ensure response is sent
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Now finish the update (this will reboot)
        if (!ota_manager_finish_streaming_update(ota_handle)) {
            ESP_LOGE(TAG, "Failed to finish streaming OTA update");
            // Response already sent, but update failed - device will not reboot
            return ESP_FAIL;
        }
        
        // This should never be reached as ota_manager_finish_streaming_update() reboots
        return resp_err;
    }
    
    // Handle URL-based update (existing JSON method)
    if (strstr(content_type, "application/json") == NULL) {
        ESP_LOGW(TAG, "Unsupported Content-Type for OTA update: %s", content_type);
        return send_json_error(req, "Unsupported Content-Type. Use multipart/form-data for file upload or application/json for URL", 400);
    }
    
    char content[256];
    int bytes_received = httpd_req_recv(req, content, sizeof(content) - 1);
    if (bytes_received <= 0) {
        ESP_LOGE(TAG, "Failed to read request body");
        return send_json_error(req, "Failed to read request body", 500);
    }
    content[bytes_received] = '\0';
    
    cJSON *json = cJSON_Parse(content);
    if (json == NULL) {
        ESP_LOGW(TAG, "Invalid JSON in request");
        return send_json_error(req, "Invalid JSON", 400);
    }
    
    cJSON *item = cJSON_GetObjectItem(json, "url");
    if (item == NULL || !cJSON_IsString(item)) {
        cJSON_Delete(json);
        return send_json_error(req, "Missing or invalid URL", 400);
    }
    
    const char *url = cJSON_GetStringValue(item);
    if (url == NULL) {
        cJSON_Delete(json);
        return send_json_error(req, "Invalid URL", 400);
    }
    cJSON_Delete(json);
    
    ESP_LOGI(TAG, "Starting OTA update from URL: %s", url);
    bool success = ota_manager_start_update(url);
    
    cJSON *response = cJSON_CreateObject();
    if (success) {
        cJSON_AddStringToObject(response, "status", "ok");
        cJSON_AddStringToObject(response, "message", "OTA update started");
    } else {
        cJSON_AddStringToObject(response, "status", "error");
        cJSON_AddStringToObject(response, "message", "Failed to start OTA update");
    }
    
    return send_json_response(req, response, success ? ESP_OK : ESP_FAIL);
}

// GET /api/ota/status - Get OTA status
static esp_err_t api_ota_status_handler(httpd_req_t *req)
{
    ota_status_info_t status_info;
    if (!ota_manager_get_status(&status_info)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    cJSON *json = cJSON_CreateObject();
    const char *status_str;
    switch (status_info.status) {
        case OTA_STATUS_IDLE:
            status_str = "idle";
            break;
        case OTA_STATUS_IN_PROGRESS:
            status_str = "in_progress";
            break;
        case OTA_STATUS_COMPLETE:
            status_str = "complete";
            break;
        case OTA_STATUS_ERROR:
            status_str = "error";
            break;
        default:
            status_str = "unknown";
            break;
    }
    
    cJSON_AddStringToObject(json, "status", status_str);
    cJSON_AddNumberToObject(json, "progress", status_info.progress);
    cJSON_AddStringToObject(json, "message", status_info.message);
    
    return send_json_response(req, json, ESP_OK);
}

// GET /api/modbus - Get Modbus enabled state
static esp_err_t api_get_modbus_handler(httpd_req_t *req)
{
    // Use cached value if available, otherwise load from NVS and cache it
    if (!s_modbus_enabled_cached) {
        s_cached_modbus_enabled = system_modbus_enabled_load();
        s_modbus_enabled_cached = true;
    }
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "enabled", s_cached_modbus_enabled);
    
    return send_json_response(req, json, ESP_OK);
}

// POST /api/modbus - Set Modbus enabled state
static esp_err_t api_post_modbus_handler(httpd_req_t *req)
{
    char content[128];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    cJSON *json = cJSON_Parse(content);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *item = cJSON_GetObjectItem(json, "enabled");
    if (item == NULL || !cJSON_IsBool(item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid 'enabled' field");
        return ESP_FAIL;
    }
    
    bool enabled = cJSON_IsTrue(item);
    cJSON_Delete(json);
    
    if (!system_modbus_enabled_save(enabled)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save Modbus state");
        return ESP_FAIL;
    }
    
    // Update cache when state changes
    s_cached_modbus_enabled = enabled;
    s_modbus_enabled_cached = true;
    
    // Apply the change immediately
    if (enabled) {
        if (!modbus_tcp_init()) {
            ESP_LOGW(TAG, "Failed to initialize ModbusTCP");
        } else {
            if (!modbus_tcp_start()) {
                ESP_LOGW(TAG, "Failed to start ModbusTCP server");
            }
        }
    } else {
        modbus_tcp_stop();
    }
    
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddBoolToObject(response, "enabled", enabled);
    cJSON_AddStringToObject(response, "message", "Modbus state saved successfully");
    
    return send_json_response(req, response, ESP_OK);
}

// GET /api/assemblies/sizes - Get assembly sizes
static esp_err_t api_get_assemblies_sizes_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "input_assembly_size", sizeof(g_assembly_data064));
    cJSON_AddNumberToObject(json, "output_assembly_size", sizeof(g_assembly_data096));
    
    return send_json_response(req, json, ESP_OK);
}

// GET /api/status - Get assembly data for status pages
static esp_err_t api_get_status_handler(httpd_req_t *req)
{
    SemaphoreHandle_t mutex = sample_application_get_assembly_mutex();
    if (mutex == NULL) {
        return send_json_error(req, "Assembly mutex not available", 500);
    }
    
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return send_json_error(req, "Failed to acquire assembly mutex", 500);
    }
    
    cJSON *json = cJSON_CreateObject();
    
    // Input assembly 100 (g_assembly_data064)
    cJSON *input_assembly = cJSON_CreateObject();
    cJSON *input_bytes = cJSON_CreateArray();
    for (int i = 0; i < sizeof(g_assembly_data064); i++) {
        cJSON_AddItemToArray(input_bytes, cJSON_CreateNumber(g_assembly_data064[i]));
    }
    cJSON_AddItemToObject(input_assembly, "raw_bytes", input_bytes);
    cJSON_AddItemToObject(json, "input_assembly_100", input_assembly);
    
    // Output assembly 150 (g_assembly_data096)
    cJSON *output_assembly = cJSON_CreateObject();
    cJSON *output_bytes = cJSON_CreateArray();
    for (int i = 0; i < sizeof(g_assembly_data096); i++) {
        cJSON_AddItemToArray(output_bytes, cJSON_CreateNumber(g_assembly_data096[i]));
    }
    cJSON_AddItemToObject(output_assembly, "raw_bytes", output_bytes);
    cJSON_AddItemToObject(json, "output_assembly_150", output_assembly);
    
    xSemaphoreGive(mutex);
    return send_json_response(req, json, ESP_OK);
}


// GET /api/i2c/pullup - Get I2C pull-up enabled state
static esp_err_t api_get_i2c_pullup_handler(httpd_req_t *req)
{
    bool enabled = system_i2c_internal_pullup_load();
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "enabled", enabled);
    
    return send_json_response(req, json, ESP_OK);
}

// POST /api/i2c/pullup - Set I2C pull-up enabled state
static esp_err_t api_post_i2c_pullup_handler(httpd_req_t *req)
{
    char content[128];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    cJSON *json = cJSON_Parse(content);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *item = cJSON_GetObjectItem(json, "enabled");
    if (item == NULL || !cJSON_IsBool(item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid 'enabled' field");
        return ESP_FAIL;
    }
    
    bool enabled = cJSON_IsTrue(item);
    cJSON_Delete(json);
    
    if (!system_i2c_internal_pullup_save(enabled)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save I2C pull-up setting");
        return ESP_FAIL;
    }
    
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddBoolToObject(response, "enabled", enabled);
    cJSON_AddStringToObject(response, "message", "I2C pull-up setting saved. Restart required for changes to take effect.");
    
    return send_json_response(req, response, ESP_OK);
}

// Forward declaration for I2C bus handle
extern i2c_master_bus_handle_t sample_application_get_i2c_bus_handle(void);

// GET /api/mpu6050/enabled - Get MPU6050 enabled state
static esp_err_t api_get_mpu6050_enabled_handler(httpd_req_t *req)
{
    // Use cached value if available, otherwise load from NVS and cache it
    if (!s_mpu6050_enabled_cached) {
        s_cached_mpu6050_enabled = system_mpu6050_enabled_load();
        s_mpu6050_enabled_cached = true;
    }
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "enabled", s_cached_mpu6050_enabled);
    
    return send_json_response(req, json, ESP_OK);
}

// POST /api/mpu6050/enabled - Set MPU6050 enabled state
static esp_err_t api_post_mpu6050_enabled_handler(httpd_req_t *req)
{
    char content[128];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    cJSON *json = cJSON_Parse(content);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *item = cJSON_GetObjectItem(json, "enabled");
    if (item == NULL || !cJSON_IsBool(item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid 'enabled' field");
        return ESP_FAIL;
    }
    
    bool enabled = cJSON_IsTrue(item);
    cJSON_Delete(json);
    
    if (!system_mpu6050_enabled_save(enabled)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save MPU6050 state");
        return ESP_FAIL;
    }
    
    // Update cache when state changes
    s_cached_mpu6050_enabled = enabled;
    s_mpu6050_enabled_cached = true;
    
    // Update main.c cache so I/O task picks up the change immediately
    sample_application_set_mpu6050_enabled(enabled);
    
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddBoolToObject(response, "enabled", enabled);
    cJSON_AddStringToObject(response, "message", "MPU6050 state saved successfully");
    
    return send_json_response(req, response, ESP_OK);
}

// GET /api/mpu6050/byteoffset - Get MPU6050 data byte offset
static esp_err_t api_get_mpu6050_byteoffset_handler(httpd_req_t *req)
{
    // Use cached value if available, otherwise load from NVS and cache it
    if (!s_mpu6050_byte_start_cached) {
        s_cached_mpu6050_byte_start = system_mpu6050_byte_start_load();
        s_mpu6050_byte_start_cached = true;
    }
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "start_byte", s_cached_mpu6050_byte_start);
    cJSON_AddNumberToObject(json, "end_byte", s_cached_mpu6050_byte_start + 19);
    // Format range string
    char range_str[16];
    snprintf(range_str, sizeof(range_str), "%d-%d", s_cached_mpu6050_byte_start, s_cached_mpu6050_byte_start + 19);
    cJSON_AddStringToObject(json, "range", range_str);
    
    return send_json_response(req, json, ESP_OK);
}

// POST /api/mpu6050/byteoffset - Set MPU6050 data byte offset
static esp_err_t api_post_mpu6050_byteoffset_handler(httpd_req_t *req)
{
    char content[128];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    cJSON *json = cJSON_Parse(content);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *item = cJSON_GetObjectItem(json, "start_byte");
    if (item == NULL || !cJSON_IsNumber(item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid 'start_byte' field");
        return ESP_FAIL;
    }
    
    uint8_t start_byte = (uint8_t)cJSON_GetNumberValue(item);
    cJSON_Delete(json);
    
    // Validate: must be 0-12 (MPU6050 uses 20 bytes: 5 int32_t)
    if (start_byte > 12) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid start_byte (must be 0-12, uses 20 bytes)");
        return ESP_FAIL;
    }
    
    // Check bounds
    const uint8_t mpu6050_byte_count = 20;
    if (start_byte + mpu6050_byte_count > sizeof(g_assembly_data064)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Byte range exceeds assembly size");
        return ESP_FAIL;
    }
    
    if (!system_mpu6050_byte_start_save(start_byte)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save MPU6050 byte offset");
        return ESP_FAIL;
    }
    
    // Update cache when byte offset changes
    s_cached_mpu6050_byte_start = start_byte;
    s_mpu6050_byte_start_cached = true;
    
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "start_byte", start_byte);
    cJSON_AddNumberToObject(response, "end_byte", start_byte + 19);
    char range_str[16];
    snprintf(range_str, sizeof(range_str), "%d-%d", start_byte, start_byte + 19);
    cJSON_AddStringToObject(response, "range", range_str);
    cJSON_AddStringToObject(response, "message", "MPU6050 byte offset saved successfully");
    
    return send_json_response(req, response, ESP_OK);
}

// GET /api/mpu6050/status - Get MPU6050 sensor status and current readings
static esp_err_t api_get_mpu6050_status_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();
    
    // Get configured MPU6050 data start byte offset
    uint8_t offset;
    if (!s_mpu6050_byte_start_cached) {
        offset = system_mpu6050_byte_start_load();
        s_cached_mpu6050_byte_start = offset;
        s_mpu6050_byte_start_cached = true;
    } else {
        offset = s_cached_mpu6050_byte_start;
    }
    
    // Validate offset to prevent buffer overflow (MPU6050 data needs 20 bytes: 5 int32_t)
    if (offset + 19 >= sizeof(g_assembly_data064)) {
        return send_json_error(req, "Invalid MPU6050 byte offset configuration", 500);
    }
    
    // Get assembly mutex for thread-safe access
    SemaphoreHandle_t assembly_mutex = sample_application_get_assembly_mutex();
    if (assembly_mutex != NULL) {
        xSemaphoreTake(assembly_mutex, portMAX_DELAY);
    }
    
    // Read MPU6050 orientation and pressure data from assembly buffer using configured offset
    // Format: 20 bytes (5 int32_t: roll, pitch, ground_angle, bottom_pressure, top_pressure as little-endian)
    // Values are scaled integers: degrees * 10000, pressure * 1000
    // Use memcpy to properly handle endianness and sign extension
    int32_t roll_scaled, pitch_scaled, ground_angle_scaled, bottom_pressure_scaled, top_pressure_scaled;
    memcpy(&roll_scaled, &g_assembly_data064[offset + 0], sizeof(int32_t));
    memcpy(&pitch_scaled, &g_assembly_data064[offset + 4], sizeof(int32_t));
    memcpy(&ground_angle_scaled, &g_assembly_data064[offset + 8], sizeof(int32_t));
    memcpy(&bottom_pressure_scaled, &g_assembly_data064[offset + 12], sizeof(int32_t));
    memcpy(&top_pressure_scaled, &g_assembly_data064[offset + 16], sizeof(int32_t));
    
    // Convert scaled integers back to degrees and PSI for JSON response
    float roll = roll_scaled / 10000.0f;
    float pitch = pitch_scaled / 10000.0f;
    float ground_angle = ground_angle_scaled / 10000.0f;
    float bottom_pressure = bottom_pressure_scaled / 1000.0f;
    float top_pressure = top_pressure_scaled / 1000.0f;
    
    if (assembly_mutex != NULL) {
        xSemaphoreGive(assembly_mutex);
    }
    
    // Get enabled state
    bool enabled;
    if (!s_mpu6050_enabled_cached) {
        enabled = system_mpu6050_enabled_load();
        s_cached_mpu6050_enabled = enabled;
        s_mpu6050_enabled_cached = true;
    } else {
        enabled = s_cached_mpu6050_enabled;
    }
    
    // Add orientation data (as floats for JSON, but stored as scaled integers in assembly)
    cJSON_AddNumberToObject(json, "roll", roll);
    cJSON_AddNumberToObject(json, "pitch", pitch);
    cJSON_AddNumberToObject(json, "ground_angle", ground_angle);
    
    // Add pressure data
    cJSON_AddNumberToObject(json, "bottom_pressure_psi", bottom_pressure);
    cJSON_AddNumberToObject(json, "top_pressure_psi", top_pressure);
    
    // Also add raw scaled integer values for reference
    cJSON_AddNumberToObject(json, "roll_scaled", roll_scaled);
    cJSON_AddNumberToObject(json, "pitch_scaled", pitch_scaled);
    cJSON_AddNumberToObject(json, "ground_angle_scaled", ground_angle_scaled);
    cJSON_AddNumberToObject(json, "bottom_pressure_scaled", bottom_pressure_scaled);
    cJSON_AddNumberToObject(json, "top_pressure_scaled", top_pressure_scaled);
    
    // Add configuration info
    cJSON_AddBoolToObject(json, "enabled", enabled);
    cJSON_AddNumberToObject(json, "byte_offset", offset);
    cJSON_AddNumberToObject(json, "byte_range_start", offset);
    cJSON_AddNumberToObject(json, "byte_range_end", offset + 19);
    
    return send_json_response(req, json, ESP_OK);
}

// GET /api/mpu6050/toolweight - Get tool weight
static esp_err_t api_get_tool_weight_handler(httpd_req_t *req)
{
    uint8_t tool_weight = system_tool_weight_load();
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "tool_weight", tool_weight);
    
    return send_json_response(req, json, ESP_OK);
}

// POST /api/mpu6050/toolweight - Set tool weight
static esp_err_t api_post_tool_weight_handler(httpd_req_t *req)
{
    char content[128];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    cJSON *json = cJSON_Parse(content);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *item = cJSON_GetObjectItem(json, "tool_weight");
    if (item == NULL || !cJSON_IsNumber(item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid 'tool_weight' field");
        return ESP_FAIL;
    }
    
    int tool_weight_int = (int)cJSON_GetNumberValue(item);
    cJSON_Delete(json);
    
    if (tool_weight_int <= 0 || tool_weight_int > 255) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid tool_weight (must be 1-255 lbs)");
        return ESP_FAIL;
    }
    
    uint8_t tool_weight = (uint8_t)tool_weight_int;
    
    if (!system_tool_weight_save(tool_weight)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save tool weight");
        return ESP_FAIL;
    }
    
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "tool_weight", tool_weight);
    cJSON_AddStringToObject(response, "message", "Tool weight saved successfully");
    
    return send_json_response(req, response, ESP_OK);
}


// GET /api/mpu6050/tipforce - Get tip force
static esp_err_t api_get_tip_force_handler(httpd_req_t *req)
{
    uint8_t tip_force = system_tip_force_load();
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "tip_force", tip_force);
    
    return send_json_response(req, json, ESP_OK);
}

// POST /api/mpu6050/tipforce - Set tip force
static esp_err_t api_post_tip_force_handler(httpd_req_t *req)
{
    char content[128];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    cJSON *json = cJSON_Parse(content);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *item = cJSON_GetObjectItem(json, "tip_force");
    if (item == NULL || !cJSON_IsNumber(item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid 'tip_force' field");
        return ESP_FAIL;
    }
    
    int tip_force_int = (int)cJSON_GetNumberValue(item);
    cJSON_Delete(json);
    
    if (tip_force_int <= 0 || tip_force_int > 255) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid tip_force (must be 1-255 lbs)");
        return ESP_FAIL;
    }
    
    uint8_t tip_force = (uint8_t)tip_force_int;
    
    if (!system_tip_force_save(tip_force)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save tip force");
        return ESP_FAIL;
    }
    
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "tip_force", tip_force);
    cJSON_AddStringToObject(response, "message", "Tip force saved successfully");
    
    return send_json_response(req, response, ESP_OK);
}

// GET /api/mpu6050/cylinderbore - Get cylinder bore size
static esp_err_t api_get_cylinder_bore_handler(httpd_req_t *req)
{
    float cylinder_bore = system_cylinder_bore_load();
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "cylinder_bore", cylinder_bore);
    
    return send_json_response(req, json, ESP_OK);
}

// POST /api/mpu6050/cylinderbore - Set cylinder bore size
static esp_err_t api_post_cylinder_bore_handler(httpd_req_t *req)
{
    char content[128];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    cJSON *json = cJSON_Parse(content);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *item = cJSON_GetObjectItem(json, "cylinder_bore");
    if (item == NULL || !cJSON_IsNumber(item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid 'cylinder_bore' field");
        return ESP_FAIL;
    }
    
    double cylinder_bore_double = cJSON_GetNumberValue(item);
    cJSON_Delete(json);
    
    if (cylinder_bore_double <= 0.0 || cylinder_bore_double > 10.0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid cylinder_bore (must be between 0.1 and 10.0 inches)");
        return ESP_FAIL;
    }
    
    cJSON *response = cJSON_CreateObject();
    if (system_cylinder_bore_save((float)cylinder_bore_double)) {
        cJSON_AddStringToObject(response, "status", "ok");
        cJSON_AddNumberToObject(response, "cylinder_bore", cylinder_bore_double);
        cJSON_AddStringToObject(response, "message", "Cylinder bore saved successfully");
    } else {
        cJSON_AddStringToObject(response, "status", "error");
        cJSON_AddStringToObject(response, "message", "Failed to save cylinder bore");
    }
    
    return send_json_response(req, response, ESP_OK);
}

// GET /api/logs - Get system logs
static esp_err_t api_get_logs_handler(httpd_req_t *req)
{
    if (!log_buffer_is_enabled()) {
        return send_json_error(req, "Log buffer not enabled", 503);
    }
    
    // Get log buffer size
    size_t log_size = log_buffer_get_size();
    
    // Allocate buffer for logs (limit to 32KB for API response)
    size_t buffer_size = (log_size < 32 * 1024) ? log_size + 1 : 32 * 1024;
    char *log_buffer = (char *)malloc(buffer_size);
    if (log_buffer == NULL) {
        return send_json_error(req, "Failed to allocate memory for logs", 500);
    }
    
    // Get logs
    size_t bytes_read = log_buffer_get(log_buffer, buffer_size);
    
    // Create JSON response
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "status", "ok");
    cJSON_AddStringToObject(json, "logs", log_buffer);
    cJSON_AddNumberToObject(json, "size", bytes_read);
    cJSON_AddNumberToObject(json, "total_size", log_size);
    cJSON_AddBoolToObject(json, "truncated", bytes_read < log_size);
    
    free(log_buffer);
    
    return send_json_response(req, json, ESP_OK);
}

// Helper function to convert IP string to uint32_t (network byte order)
static uint32_t ip_string_to_uint32(const char *ip_str)
{
    if (ip_str == NULL || strlen(ip_str) == 0) {
        return 0;
    }
    struct in_addr addr;
    if (inet_aton(ip_str, &addr) == 0) {
        return 0;
    }
    return addr.s_addr;
}

// Helper function to convert uint32_t (network byte order) to IP string
static void ip_uint32_to_string(uint32_t ip, char *buf, size_t buf_size)
{
    struct in_addr addr;
    addr.s_addr = ip;
    const char *ip_str = inet_ntoa(addr);
    if (ip_str != NULL) {
        strncpy(buf, ip_str, buf_size - 1);
        buf[buf_size - 1] = '\0';
    } else {
        buf[0] = '\0';
    }
}

// GET /api/ipconfig - Get IP configuration
static esp_err_t api_get_ipconfig_handler(httpd_req_t *req)
{
    // Always read from OpENer's g_tcpip (single source of truth)
    bool use_dhcp = (g_tcpip.config_control & kTcpipCfgCtrlMethodMask) == kTcpipCfgCtrlDhcp;
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "use_dhcp", use_dhcp);
    
    char ip_str[16];
    ip_uint32_to_string(g_tcpip.interface_configuration.ip_address, ip_str, sizeof(ip_str));
    cJSON_AddStringToObject(json, "ip_address", ip_str);
    
    ip_uint32_to_string(g_tcpip.interface_configuration.network_mask, ip_str, sizeof(ip_str));
    cJSON_AddStringToObject(json, "netmask", ip_str);
    
    ip_uint32_to_string(g_tcpip.interface_configuration.gateway, ip_str, sizeof(ip_str));
    cJSON_AddStringToObject(json, "gateway", ip_str);
    
    ip_uint32_to_string(g_tcpip.interface_configuration.name_server, ip_str, sizeof(ip_str));
    cJSON_AddStringToObject(json, "dns1", ip_str);
    
    ip_uint32_to_string(g_tcpip.interface_configuration.name_server_2, ip_str, sizeof(ip_str));
    cJSON_AddStringToObject(json, "dns2", ip_str);
    
    return send_json_response(req, json, ESP_OK);
}

// POST /api/ipconfig - Set IP configuration
static esp_err_t api_post_ipconfig_handler(httpd_req_t *req)
{
    char content[512];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    cJSON *json = cJSON_Parse(content);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    // Update g_tcpip directly (single source of truth)
    cJSON *item = cJSON_GetObjectItem(json, "use_dhcp");
    if (item != NULL && cJSON_IsBool(item)) {
        bool use_dhcp = cJSON_IsTrue(item);
        if (use_dhcp) {
            g_tcpip.config_control &= ~kTcpipCfgCtrlMethodMask;
            g_tcpip.config_control |= kTcpipCfgCtrlDhcp;
            g_tcpip.interface_configuration.ip_address = 0;
            g_tcpip.interface_configuration.network_mask = 0;
            g_tcpip.interface_configuration.gateway = 0;
        } else {
            g_tcpip.config_control &= ~kTcpipCfgCtrlMethodMask;
            g_tcpip.config_control |= kTcpipCfgCtrlStaticIp;
        }
    }
    
    if ((g_tcpip.config_control & kTcpipCfgCtrlMethodMask) == kTcpipCfgCtrlStaticIp) {
        // Only update IP settings if using static IP
        item = cJSON_GetObjectItem(json, "ip_address");
        if (item != NULL && cJSON_IsString(item)) {
            g_tcpip.interface_configuration.ip_address = ip_string_to_uint32(cJSON_GetStringValue(item));
        }
        
        item = cJSON_GetObjectItem(json, "netmask");
        if (item != NULL && cJSON_IsString(item)) {
            g_tcpip.interface_configuration.network_mask = ip_string_to_uint32(cJSON_GetStringValue(item));
        }
        
        item = cJSON_GetObjectItem(json, "gateway");
        if (item != NULL && cJSON_IsString(item)) {
            g_tcpip.interface_configuration.gateway = ip_string_to_uint32(cJSON_GetStringValue(item));
        }
    }
    
    item = cJSON_GetObjectItem(json, "dns1");
    if (item != NULL && cJSON_IsString(item)) {
        g_tcpip.interface_configuration.name_server = ip_string_to_uint32(cJSON_GetStringValue(item));
    }
    
    item = cJSON_GetObjectItem(json, "dns2");
    if (item != NULL && cJSON_IsString(item)) {
        g_tcpip.interface_configuration.name_server_2 = ip_string_to_uint32(cJSON_GetStringValue(item));
    }
    
    cJSON_Delete(json);
    
    // Save directly to OpENer's NVS
    if (NvTcpipStore(&g_tcpip) != kEipStatusOk) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save IP configuration");
        return ESP_FAIL;
    }
    
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "message", "IP configuration saved successfully. Reboot required to apply changes.");
    
    return send_json_response(req, response, ESP_OK);
}

void webui_register_api_handlers(httpd_handle_t server)
{
    if (server == NULL) {
        ESP_LOGE(TAG, "Cannot register API handlers: server handle is NULL!");
        return;
    }
    
    ESP_LOGI(TAG, "Registering API handlers...");
    
    // POST /api/ota/update
    httpd_uri_t ota_update_uri = {
        .uri       = "/api/ota/update",
        .method    = HTTP_POST,
        .handler   = api_ota_update_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &ota_update_uri);
    
    // GET /api/ota/status
    httpd_uri_t ota_status_uri = {
        .uri       = "/api/ota/status",
        .method    = HTTP_GET,
        .handler   = api_ota_status_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &ota_status_uri);
    
    // POST /api/reboot
    httpd_uri_t reboot_uri = {
        .uri       = "/api/reboot",
        .method    = HTTP_POST,
        .handler   = api_reboot_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &reboot_uri);
    
    // GET /api/modbus
    httpd_uri_t get_modbus_uri = {
        .uri       = "/api/modbus",
        .method    = HTTP_GET,
        .handler   = api_get_modbus_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &get_modbus_uri);
    
    // POST /api/modbus
    httpd_uri_t post_modbus_uri = {
        .uri       = "/api/modbus",
        .method    = HTTP_POST,
        .handler   = api_post_modbus_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &post_modbus_uri);
    
    // GET /api/mpu6050/enabled
    httpd_uri_t get_mpu6050_enabled_uri = {
        .uri       = "/api/mpu6050/enabled",
        .method    = HTTP_GET,
        .handler   = api_get_mpu6050_enabled_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &get_mpu6050_enabled_uri);
    
    // POST /api/mpu6050/enabled
    httpd_uri_t post_mpu6050_enabled_uri = {
        .uri       = "/api/mpu6050/enabled",
        .method    = HTTP_POST,
        .handler   = api_post_mpu6050_enabled_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &post_mpu6050_enabled_uri);
    
    // GET /api/mpu6050/byteoffset
    httpd_uri_t get_mpu6050_byteoffset_uri = {
        .uri       = "/api/mpu6050/byteoffset",
        .method    = HTTP_GET,
        .handler   = api_get_mpu6050_byteoffset_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &get_mpu6050_byteoffset_uri);
    
    // POST /api/mpu6050/byteoffset
    httpd_uri_t post_mpu6050_byteoffset_uri = {
        .uri       = "/api/mpu6050/byteoffset",
        .method    = HTTP_POST,
        .handler   = api_post_mpu6050_byteoffset_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &post_mpu6050_byteoffset_uri);
    
    // GET /api/mpu6050/status
    httpd_uri_t get_mpu6050_status_uri = {
        .uri       = "/api/mpu6050/status",
        .method    = HTTP_GET,
        .handler   = api_get_mpu6050_status_handler,
        .user_ctx  = NULL
    };
    esp_err_t ret_mpu6050_status = httpd_register_uri_handler(server, &get_mpu6050_status_uri);
    if (ret_mpu6050_status != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register GET /api/mpu6050/status: %s", esp_err_to_name(ret_mpu6050_status));
    } else {
        ESP_LOGI(TAG, "Registered GET /api/mpu6050/status handler");
    }
    
    // GET /api/mpu6050/toolweight
    httpd_uri_t get_tool_weight_uri = {
        .uri       = "/api/mpu6050/toolweight",
        .method    = HTTP_GET,
        .handler   = api_get_tool_weight_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &get_tool_weight_uri);
    
    // POST /api/mpu6050/toolweight
    httpd_uri_t post_tool_weight_uri = {
        .uri       = "/api/mpu6050/toolweight",
        .method    = HTTP_POST,
        .handler   = api_post_tool_weight_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &post_tool_weight_uri);
    
    // GET /api/mpu6050/tipforce
    httpd_uri_t get_tip_force_uri = {
        .uri       = "/api/mpu6050/tipforce",
        .method    = HTTP_GET,
        .handler   = api_get_tip_force_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &get_tip_force_uri);
    
    // POST /api/mpu6050/tipforce
    httpd_uri_t post_tip_force_uri = {
        .uri       = "/api/mpu6050/tipforce",
        .method    = HTTP_POST,
        .handler   = api_post_tip_force_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &post_tip_force_uri);
    
    // GET /api/mpu6050/cylinderbore
    httpd_uri_t get_cylinder_bore_uri = {
        .uri       = "/api/mpu6050/cylinderbore",
        .method    = HTTP_GET,
        .handler   = api_get_cylinder_bore_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &get_cylinder_bore_uri);
    
    // POST /api/mpu6050/cylinderbore
    httpd_uri_t post_cylinder_bore_uri = {
        .uri       = "/api/mpu6050/cylinderbore",
        .method    = HTTP_POST,
        .handler   = api_post_cylinder_bore_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &post_cylinder_bore_uri);
    
    
    // GET /api/assemblies/sizes
    httpd_uri_t get_assemblies_sizes_uri = {
        .uri       = "/api/assemblies/sizes",
        .method    = HTTP_GET,
        .handler   = api_get_assemblies_sizes_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &get_assemblies_sizes_uri);
    
    // GET /api/status - Get assembly data for status pages
    httpd_uri_t get_status_uri = {
        .uri       = "/api/status",
        .method    = HTTP_GET,
        .handler   = api_get_status_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &get_status_uri);
    
    // GET /api/i2c/pullup
    httpd_uri_t get_i2c_pullup_uri = {
        .uri       = "/api/i2c/pullup",
        .method    = HTTP_GET,
        .handler   = api_get_i2c_pullup_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &get_i2c_pullup_uri);
    
    // POST /api/i2c/pullup
    httpd_uri_t post_i2c_pullup_uri = {
        .uri       = "/api/i2c/pullup",
        .method    = HTTP_POST,
        .handler   = api_post_i2c_pullup_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &post_i2c_pullup_uri);
    
    
    // GET /api/logs - Get system logs
    httpd_uri_t get_logs_uri = {
        .uri       = "/api/logs",
        .method    = HTTP_GET,
        .handler   = api_get_logs_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &get_logs_uri);
    
    // GET /api/ipconfig
    httpd_uri_t get_ipconfig_uri = {
        .uri       = "/api/ipconfig",
        .method    = HTTP_GET,
        .handler   = api_get_ipconfig_handler,
        .user_ctx  = NULL
    };
    esp_err_t ret = httpd_register_uri_handler(server, &get_ipconfig_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register GET /api/ipconfig: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Registered GET /api/ipconfig handler");
    }
    
    // POST /api/ipconfig
    httpd_uri_t post_ipconfig_uri = {
        .uri       = "/api/ipconfig",
        .method    = HTTP_POST,
        .handler   = api_post_ipconfig_handler,
        .user_ctx  = NULL
    };
    ret = httpd_register_uri_handler(server, &post_ipconfig_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register POST /api/ipconfig: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Registered POST /api/ipconfig handler");
    }
    
    ESP_LOGI(TAG, "API handler registration complete");
}

