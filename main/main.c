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

/**
 * @file main.c
 * @brief Main application entry point for ESP32-P4 EtherNet/IP device
 *
 * ADDRESS CONFLICT DETECTION (ACD) IMPLEMENTATION
 * ===============================================
 *
 * This file implements RFC 5227 compliant Address Conflict Detection (ACD) for
 * static IP addresses. ACD ensures that IP addresses are not assigned until
 * confirmed safe to use, preventing network conflicts.
 *
 * Architecture:
 * ------------
 * - Static IP: Full RFC 5227 compliance
 *   * Probe phase: 3 ARP probes from 0.0.0.0 with random 1-2 second intervals
 *   * Announce phase: 2 ARP announcements after successful probe
 *   * Ongoing defense: Periodic ARP probes every ~90 seconds (configurable)
 *   * Total time: ~8-10 seconds for initial IP assignment
 *
 * - DHCP: Simplified ACD (not RFC 5227 compliant)
 *   * Single ARP probe with 500ms timeout
 *   * Fast conflict detection (~1 second)
 *   * Handled internally by lwIP DHCP client
 *
 * Implementation Details:
 * ----------------------
 * 1. Legacy Mode (LWIP_ACD_RFC5227_COMPLIANT_STATIC=0):
 *    - ACD probe sequence runs BEFORE IP assignment
 *    - IP is assigned only after ACD confirms no conflict
 *    - Uses tcpip_perform_acd() to coordinate probe sequence
 *
 * 2. RFC 5227 Mode (LWIP_ACD_RFC5227_COMPLIANT_STATIC=1):
 *    - Uses lwIP's netif_set_addr_with_acd() API
 *    - IP assignment deferred until ACD completes
 *    - More robust but requires RFC 5227 support in lwIP
 *
 * 3. Retry Logic (CONFIG_OPENER_ACD_RETRY_ENABLED):
 *    - On conflict, removes IP and schedules retry after delay
 *    - Configurable max attempts and retry delay
 *    - Prevents infinite retry loops
 *
 * 4. User LED Indication:
 *    - GPIO27 blinks during normal operation
 *    - Goes solid on ACD conflict detection
 *    - Visual feedback for network issues
 *
 * Thread Safety:
 * -------------
 * - ACD operations use tcpip_callback_with_block() to ensure execution on tcpip thread
 * - Context structures allocated on heap to prevent stack corruption
 * - Semaphores coordinate async callback execution
 *
 * Configuration:
 * --------------
 * - CONFIG_OPENER_ACD_PROBE_NUM: Number of probes (default: 3)
 * - CONFIG_OPENER_ACD_PROBE_MIN_MS: Minimum probe interval (default: 1000ms)
 * - CONFIG_OPENER_ACD_PROBE_MAX_MS: Maximum probe interval (default: 2000ms)
 * - CONFIG_OPENER_ACD_PERIODIC_DEFEND_INTERVAL_MS: Defensive ARP interval (default: 90000ms)
 * - CONFIG_OPENER_ACD_RETRY_ENABLED: Enable retry on conflict
 * - CONFIG_OPENER_ACD_RETRY_DELAY_MS: Delay before retry (default: 10000ms)
 * - CONFIG_OPENER_ACD_RETRY_MAX_ATTEMPTS: Max retry attempts (default: 5)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_eth_mac_esp.h"
#include "esp_eth_phy.h"
#include "esp_event.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "lwip/netif.h"
#include "lwip/err.h"
#include "lwip/tcpip.h"
#include "lwip/ip4_addr.h"
#include "lwip/acd.h"
#include "lwip/netifapi.h"
#include "lwip/timeouts.h"
#include "lwip/etharp.h"
#if LWIP_ACD && LWIP_ACD_RFC5227_COMPLIANT_STATIC
#include "lwip/netif_pending_ip.h"
#endif
#include "nvs_flash.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "opener.h"
#include "nvtcpip.h"
#include "ciptcpipinterface.h"
#include "sdkconfig.h"
#include "esp_netif_net_stack.h"
#include "webui.h"
#include "modbus_tcp.h"
#include "ota_manager.h"
#include "system_config.h"
#include "mpu6050.h"
#include "lsm6ds3.h"
#include "lsm6ds3_fusion.h"
#include "lsm6ds3_reg.h"
#include "driver/i2c_master.h"
#include "log_buffer.h"

// Forward declaration - function is in opener component
SemaphoreHandle_t sample_application_get_assembly_mutex(void);

void SampleApplicationSetActiveNetif(struct netif *netif);
void SampleApplicationNotifyLinkUp(void);
void SampleApplicationNotifyLinkDown(void);

// External assembly data arrays (defined in opener component)
extern uint8_t g_assembly_data064[32];  // Input Assembly 100
extern uint8_t g_assembly_data096[32];  // Output Assembly 150

static const char *TAG = "opener_main";
static struct netif *s_netif = NULL;
static SemaphoreHandle_t s_netif_mutex = NULL;
static bool s_services_initialized = false;

// IMU sensor type enumeration
typedef enum {
    IMU_TYPE_NONE = 0,
    IMU_TYPE_MPU6050 = 1,
    IMU_TYPE_LSM6DS3 = 2
} imu_type_t;

// MPU6050 device instance
static mpu6050_t s_mpu6050 = {0};
static i2c_master_dev_handle_t s_mpu6050_dev_handle = NULL;
static bool s_mpu6050_initialized = false;

// LSM6DS3 device instance
static lsm6ds3_handle_t s_lsm6ds3_handle = {0};
static lsm6ds3_complementary_filter_t s_lsm6ds3_filter = {0};
static bool s_lsm6ds3_initialized = false;

// Unified IMU state
static imu_type_t s_active_imu_type = IMU_TYPE_NONE;
static bool s_imu_enabled_cached = false;  // Cache enabled state to avoid repeated NVS reads
static SemaphoreHandle_t s_imu_state_mutex = NULL;  // Protects IMU state variables

// Sensor fusion state for absolute angle from vertical (removed - not currently used)

static TaskHandle_t s_imu_test_task_handle = NULL;
static TaskHandle_t s_imu_io_task_handle = NULL;

// I2C bus handle (initialized in app_main)
static i2c_master_bus_handle_t s_i2c_bus_handle = NULL;

// User LED state (GPIO27)
#define USER_LED_GPIO 27
static bool s_user_led_initialized = false;
static bool s_user_led_flash_enabled = false;
static TaskHandle_t s_user_led_task_handle = NULL;

// Initialize IMU state mutex (called once during initialization)
static void imu_state_mutex_init(void)
{
    if (s_imu_state_mutex == NULL) {
        s_imu_state_mutex = xSemaphoreCreateMutex();
        if (s_imu_state_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create IMU state mutex");
        }
    }
}

// Function to update IMU enabled cache (called from API)
void sample_application_set_mpu6050_enabled(bool enabled)
{
    if (s_imu_state_mutex == NULL) {
        imu_state_mutex_init();
    }
    if (s_imu_state_mutex != NULL && xSemaphoreTake(s_imu_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_imu_enabled_cached = enabled;
        xSemaphoreGive(s_imu_state_mutex);
    } else {
        // Fallback: set without mutex if mutex unavailable (shouldn't happen)
        s_imu_enabled_cached = enabled;
    }
}

// Function to update IMU enabled cache for LSM6DS3 (called from API)
void sample_application_set_lsm6ds3_enabled(bool enabled)
{
    if (s_imu_state_mutex == NULL) {
        imu_state_mutex_init();
    }
    if (s_imu_state_mutex != NULL && xSemaphoreTake(s_imu_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_imu_enabled_cached = enabled;
        xSemaphoreGive(s_imu_state_mutex);
    } else {
        // Fallback: set without mutex if mutex unavailable (shouldn't happen)
        s_imu_enabled_cached = enabled;
    }
}

// Function to trigger LSM6DS3 calibration (called from API)
// Returns ESP_OK on success, ESP_FAIL if sensor not initialized
esp_err_t sample_application_calibrate_lsm6ds3(uint32_t samples, uint32_t sample_delay_ms)
{
    if (s_imu_state_mutex == NULL) {
        imu_state_mutex_init();
    }
    
    // Check initialization status with mutex protection
    bool is_initialized = false;
    if (s_imu_state_mutex != NULL && xSemaphoreTake(s_imu_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        is_initialized = s_lsm6ds3_initialized;
        xSemaphoreGive(s_imu_state_mutex);
    } else {
        // Fallback: check without mutex
        is_initialized = s_lsm6ds3_initialized;
    }
    
    if (!is_initialized) {
        return ESP_FAIL;
    }
    
    // Perform calibration with mutex protection
    esp_err_t err = ESP_FAIL;
    if (s_imu_state_mutex != NULL && xSemaphoreTake(s_imu_state_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (s_lsm6ds3_initialized && s_i2c_bus_handle != NULL) {
            err = lsm6ds3_calibrate_gyro(&s_lsm6ds3_handle, samples, sample_delay_ms);
            if (err == ESP_OK) {
                // Save calibration to NVS
                esp_err_t save_err = lsm6ds3_save_calibration_to_nvs(&s_lsm6ds3_handle, "system");
                if (save_err != ESP_OK) {
                    ESP_LOGW(TAG, "LSM6DS3: Calibration complete but failed to save to NVS: %s", esp_err_to_name(save_err));
                }
            }
        }
        xSemaphoreGive(s_imu_state_mutex);
    }
    
    return err;
}

// Function to get LSM6DS3 calibration status (called from API)
bool sample_application_get_lsm6ds3_calibration_status(float *gyro_offset_mdps)
{
    if (s_imu_state_mutex == NULL) {
        imu_state_mutex_init();
    }
    
    bool calibrated = false;
    if (s_imu_state_mutex != NULL && xSemaphoreTake(s_imu_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_lsm6ds3_initialized) {
            calibrated = s_lsm6ds3_handle.calibration.gyro_calibrated;
            if (gyro_offset_mdps != NULL) {
                gyro_offset_mdps[0] = s_lsm6ds3_handle.calibration.gyro_offset_mdps[0];
                gyro_offset_mdps[1] = s_lsm6ds3_handle.calibration.gyro_offset_mdps[1];
                gyro_offset_mdps[2] = s_lsm6ds3_handle.calibration.gyro_offset_mdps[2];
            }
        }
        xSemaphoreGive(s_imu_state_mutex);
    } else {
        // Fallback: read without mutex
        if (s_lsm6ds3_initialized) {
            calibrated = s_lsm6ds3_handle.calibration.gyro_calibrated;
            if (gyro_offset_mdps != NULL) {
                gyro_offset_mdps[0] = s_lsm6ds3_handle.calibration.gyro_offset_mdps[0];
                gyro_offset_mdps[1] = s_lsm6ds3_handle.calibration.gyro_offset_mdps[1];
                gyro_offset_mdps[2] = s_lsm6ds3_handle.calibration.gyro_offset_mdps[2];
            }
        }
    }
    
    return calibrated;
}

static void imu_test_task(void *pvParameters);
static void imu_io_task(void *pvParameters);
static bool try_init_mpu6050(i2c_master_bus_handle_t bus_handle);
static bool try_init_lsm6ds3(i2c_master_bus_handle_t bus_handle);
static void scan_i2c_bus(i2c_master_bus_handle_t bus_handle);
#if LWIP_IPV4 && LWIP_ACD
static struct acd s_static_ip_acd;
static bool s_acd_registered = false;
static SemaphoreHandle_t s_acd_sem = NULL;
static SemaphoreHandle_t s_acd_registration_sem = NULL;  // Semaphore to wait for ACD registration
static acd_callback_enum_t s_acd_last_state = ACD_IP_OK;  // Will be set by callback when ACD completes
static bool s_acd_callback_received = false;  // Track if callback was actually received
static bool s_acd_probe_pending = false;
static esp_netif_ip_info_t s_pending_static_ip_cfg = {0};
#if LWIP_ACD_RFC5227_COMPLIANT_STATIC
static esp_netif_t *s_pending_esp_netif = NULL;  /* Store esp_netif for callback */
#endif
#if CONFIG_OPENER_ACD_RETRY_ENABLED
static TimerHandle_t s_acd_retry_timer = NULL;
static int s_acd_retry_count = 0;
static esp_netif_t *s_acd_retry_netif = NULL;
static struct netif *s_acd_retry_lwip_netif = NULL;
#endif
#endif
static bool s_opener_initialized;

static bool tcpip_config_uses_dhcp(void);
static void configure_hostname(esp_netif_t *netif);
static void opener_configure_dns(esp_netif_t *netif);

static bool ip_info_has_static_address(const esp_netif_ip_info_t *ip_info) {
    if (ip_info == NULL) {
        return false;
    }
    if (ip_info->ip.addr == 0 || ip_info->netmask.addr == 0) {
        return false;
    }
    return true;
}

static bool tcpip_config_uses_dhcp(void) {
    return (g_tcpip.config_control & kTcpipCfgCtrlMethodMask) == kTcpipCfgCtrlDhcp;
}

static bool tcpip_static_config_valid(void) {
    if ((g_tcpip.config_control & kTcpipCfgCtrlMethodMask) != kTcpipCfgCtrlStaticIp) {
        return true;
    }
    return CipTcpIpIsValidNetworkConfig(&g_tcpip.interface_configuration);
}

static void configure_hostname(esp_netif_t *netif) {
    if (g_tcpip.hostname.length > 0 && g_tcpip.hostname.string != NULL) {
        size_t length = g_tcpip.hostname.length;
        if (length > 63) {
            length = 63;
        }
        char host[64];
        memcpy(host, g_tcpip.hostname.string, length);
        host[length] = '\0';
        esp_netif_set_hostname(netif, host);
    }
}

static void opener_configure_dns(esp_netif_t *netif) {
    esp_netif_dns_info_t dns_info = {
        .ip.type = IPADDR_TYPE_V4,
        .ip.u_addr.ip4.addr = g_tcpip.interface_configuration.name_server
    };
    if (dns_info.ip.u_addr.ip4.addr != 0) {
        ESP_ERROR_CHECK(esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info));
    }

    dns_info.ip.u_addr.ip4.addr = g_tcpip.interface_configuration.name_server_2;
    if (dns_info.ip.u_addr.ip4.addr != 0) {
        ESP_ERROR_CHECK(esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns_info));
    }
}

#if LWIP_IPV4 && LWIP_ACD
typedef struct {
    struct netif *netif;
    ip4_addr_t ip;
    err_t err;
} AcdStartContext;

typedef struct {
    struct netif *netif;
    ip4_addr_t ip;
    err_t err;
} AcdStartProbeContext;

static void tcpip_try_pending_acd(esp_netif_t *netif, struct netif *lwip_netif);
static void tcpip_retry_acd_deferred(void *arg);
#if CONFIG_OPENER_ACD_RETRY_ENABLED
static void tcpip_acd_retry_timer_callback(TimerHandle_t xTimer);
static void tcpip_acd_start_retry(esp_netif_t *netif, struct netif *lwip_netif);

// Callback to start ACD probe on tcpip thread (used when direct acd_start() fails)
static void acd_start_probe_cb(void *arg) {
    AcdStartProbeContext *ctx = (AcdStartProbeContext *)arg;
    if (ctx == NULL || ctx->netif == NULL) {
        ESP_LOGE(TAG, "acd_start_probe_cb: Invalid context");
        if (ctx) free(ctx);
        return;
    }
    ESP_LOGI(TAG, "acd_start_probe_cb: Calling acd_start() for IP " IPSTR " on netif %p", 
             IP2STR(&ctx->ip), ctx->netif);
    ctx->err = acd_start(ctx->netif, &s_static_ip_acd, ctx->ip);
    ESP_LOGI(TAG, "acd_start_probe_cb: acd_start() returned err=%d", (int)ctx->err);
    free(ctx);  // Free heap-allocated context
}

// Callback for retry timer: executes retry on tcpip thread (has more stack space)
static void retry_callback(void *arg) {
    (void)arg;
    if (s_acd_retry_netif != NULL && s_acd_retry_lwip_netif != NULL) {
        ESP_LOGI(TAG, "ACD retry timer expired - restarting ACD probe sequence (attempt %d)",
                 s_acd_retry_count + 1);
        tcpip_try_pending_acd(s_acd_retry_netif, s_acd_retry_lwip_netif);
    }
}
#endif
#endif

static bool netif_has_valid_hwaddr(struct netif *netif) {
    if (netif == NULL) {
        return false;
    }
    if (netif->hwaddr_len != ETH_HWADDR_LEN) {
        return false;
    }
    for (int i = 0; i < ETH_HWADDR_LEN; ++i) {
        if (netif->hwaddr[i] != 0) {
            return true;
        }
    }
    return false;
}

// User LED control functions
static void user_led_init(void);
static void user_led_set(bool on);
static void user_led_flash_task(void *pvParameters);
static void user_led_start_flash(void);
static void user_led_stop_flash(void);

static void tcpip_acd_conflict_callback(struct netif *netif, acd_callback_enum_t state) {
    ESP_LOGI(TAG, "ACD callback received: state=%d (0=IP_OK, 1=RESTART_CLIENT, 2=DECLINE)", (int)state);
    s_acd_last_state = state;
    s_acd_callback_received = true;  // Mark that callback was actually received
    switch (state) {
        case ACD_IP_OK:
            g_tcpip.status &= ~(kTcpipStatusAcdStatus | kTcpipStatusAcdFault);
            // ACD_IP_OK means probe phase completed successfully and IP is assigned.
            // ACD now enters ONGOING state for periodic defense, so set activity = 1.
            CipTcpIpSetLastAcdActivity(1);
            // Resume LED blinking when IP is OK (no conflict)
            user_led_start_flash();
            ESP_LOGI(TAG, "ACD: IP OK - no conflict detected, entering ongoing defense phase");
#if CONFIG_OPENER_ACD_RETRY_ENABLED
            // Reset retry count on successful IP assignment
            s_acd_retry_count = 0;
            // Stop retry timer if running
            if (s_acd_retry_timer != NULL) {
                xTimerStop(s_acd_retry_timer, portMAX_DELAY);
            }
#endif
#if LWIP_ACD_RFC5227_COMPLIANT_STATIC
            /* With RFC 5227, IP is now assigned. Configure DNS and notify */
            if (netif != NULL && s_pending_esp_netif != NULL) {
                opener_configure_dns(s_pending_esp_netif);
                s_acd_probe_pending = false;
                ESP_LOGI(TAG, "RFC 5227: IP assigned after ACD confirmation");
            }
#else
            /* Legacy mode: Assign IP if it hasn't been assigned yet (callback fired after timeout) */
            if (s_acd_probe_pending && netif != NULL) {
                esp_netif_t *esp_netif = esp_netif_get_handle_from_netif_impl(netif);
                if (esp_netif != NULL && s_pending_static_ip_cfg.ip.addr != 0) {
                    ESP_LOGI(TAG, "Legacy ACD: Assigning IP " IPSTR " after callback confirmation", IP2STR(&s_pending_static_ip_cfg.ip));
                    esp_netif_set_ip_info(esp_netif, &s_pending_static_ip_cfg);
                    opener_configure_dns(esp_netif);
                    s_acd_probe_pending = false;
                }
            }
#endif
            break;
        case ACD_DECLINE:
        case ACD_RESTART_CLIENT:
            g_tcpip.status |= kTcpipStatusAcdStatus;
            g_tcpip.status |= kTcpipStatusAcdFault;
            CipTcpIpSetLastAcdActivity(3);
            // Stop LED blinking and turn solid on ACD conflict
            user_led_stop_flash();
            user_led_set(true);  // Turn LED on solid
            ESP_LOGW(TAG, "ACD: Conflict detected (state=%d) - LED set to solid", (int)state);
#if LWIP_ACD_RFC5227_COMPLIANT_STATIC
            /* With RFC 5227, IP was not assigned due to conflict */
            if (netif != NULL) {
                s_acd_probe_pending = false;
                s_pending_esp_netif = NULL;
                ESP_LOGW(TAG, "RFC 5227: IP not assigned due to conflict");
            }
#endif
#if CONFIG_OPENER_ACD_RETRY_ENABLED
            // Retry logic: On conflict, remove IP and schedule retry after delay
            if (netif != NULL) {
                esp_netif_t *esp_netif = esp_netif_get_handle_from_netif_impl(netif);
                if (esp_netif != NULL) {
                    // Check if we should retry
                    if (CONFIG_OPENER_ACD_RETRY_MAX_ATTEMPTS == 0 || 
                        s_acd_retry_count < CONFIG_OPENER_ACD_RETRY_MAX_ATTEMPTS) {
                        ESP_LOGW(TAG, "ACD: Scheduling retry (attempt %d/%d) after %dms",
                                 s_acd_retry_count + 1,
                                 CONFIG_OPENER_ACD_RETRY_MAX_ATTEMPTS == 0 ? 999 : CONFIG_OPENER_ACD_RETRY_MAX_ATTEMPTS,
                                 CONFIG_OPENER_ACD_RETRY_DELAY_MS);
                        tcpip_acd_start_retry(esp_netif, netif);
                    } else {
                        ESP_LOGE(TAG, "ACD: Max retry attempts (%d) reached - giving up",
                                 CONFIG_OPENER_ACD_RETRY_MAX_ATTEMPTS);
                    }
                }
            }
#endif
            break;
        default:
            g_tcpip.status |= kTcpipStatusAcdStatus;
            g_tcpip.status |= kTcpipStatusAcdFault;
            break;
    }
    if (s_acd_sem != NULL) {
        xSemaphoreGive(s_acd_sem);
    }
}

static void tcpip_acd_start_cb(void *arg) {
    ESP_LOGI(TAG, "tcpip_acd_start_cb: CALLBACK EXECUTING - arg=%p", arg);
    AcdStartContext *ctx = (AcdStartContext *)arg;
    if (ctx == NULL) {
        ESP_LOGE(TAG, "tcpip_acd_start_cb: NULL context");
        // Signal semaphore even on error so caller doesn't hang
        if (s_acd_registration_sem != NULL) {
            xSemaphoreGive(s_acd_registration_sem);
        }
        return;
    }
    ESP_LOGI(TAG, "tcpip_acd_start_cb: Context valid - netif=%p, ip=" IPSTR, 
             ctx->netif, IP2STR(&ctx->ip));
    ctx->err = ERR_OK;
    
    // NULL check: netif may be invalidated between context creation and callback execution
    if (ctx->netif == NULL) {
        ESP_LOGD(TAG, "tcpip_acd_start_cb: NULL netif - ACD probe cancelled");
        ctx->err = ERR_IF;
        free(ctx);
        return;
    }
    
    // If probe phase is complete, still register ACD for ongoing conflict detection
    bool probe_was_pending = s_acd_probe_pending;
    
    if (!s_acd_registered) {
        ctx->netif->acd_list = NULL;
        memset(&s_static_ip_acd, 0, sizeof(s_static_ip_acd));
        err_t add_err = acd_add(ctx->netif, &s_static_ip_acd, tcpip_acd_conflict_callback);
        if (add_err == ERR_OK) {
            s_acd_registered = true;
            ESP_LOGD(TAG, "tcpip_acd_start_cb: ACD client registered");
        } else {
            ESP_LOGE(TAG, "tcpip_acd_start_cb: acd_add() failed with err=%d", (int)add_err);
            ctx->err = ERR_IF;
            // Signal registration semaphore even on failure so caller doesn't hang
            if (s_acd_registration_sem != NULL) {
                xSemaphoreGive(s_acd_registration_sem);
            }
            free(ctx);
            return;
        }
    }
    
    // Signal registration semaphore to allow tcpip_perform_acd to wait for completion
    if (s_acd_registration_sem != NULL) {
        xSemaphoreGive(s_acd_registration_sem);
    }
    
    // If probe phase was skipped (IP already assigned), manually transition to ONGOING state
    // Otherwise, ACD will naturally transition: PROBING -> ANNOUNCING -> ONGOING
    if (!probe_was_pending) {
        // IP already assigned - manually transition to ONGOING state for periodic defensive ARPs
        acd_stop(&s_static_ip_acd);  // Stop current state first
        s_static_ip_acd.state = ACD_STATE_ONGOING;
        s_static_ip_acd.ipaddr = ctx->ip;
        s_static_ip_acd.sent_num = 0;
        s_static_ip_acd.lastconflict = 0;
        s_static_ip_acd.num_conflicts = 0;
        
        // Re-add to netif's acd_list so timer processes it
        acd_add(ctx->netif, &s_static_ip_acd, tcpip_acd_conflict_callback);
        
        // Set activity = 1 (OngoingDetection) since we're entering ONGOING state
        CipTcpIpSetLastAcdActivity(1);
        
        // Set ttw to defense interval so timer counts down before first probe
#ifdef CONFIG_OPENER_ACD_PERIODIC_DEFEND_INTERVAL_MS
        if (CONFIG_OPENER_ACD_PERIODIC_DEFEND_INTERVAL_MS > 0) {
            const uint16_t timer_interval_ms = 100;
            s_static_ip_acd.ttw = (uint16_t)((CONFIG_OPENER_ACD_PERIODIC_DEFEND_INTERVAL_MS + timer_interval_ms - 1) / timer_interval_ms);
        } else {
            s_static_ip_acd.ttw = 0;
        }
#else
        s_static_ip_acd.ttw = 100;  // Default 10 seconds
#endif
    }
    // If probe_was_pending, ACD is already running via acd_start() - don't stop it
    // It will naturally transition: PROBING -> ANNOUNCING -> ONGOING
    ctx->err = ERR_OK;
    
    // Free heap-allocated context (allocated on heap to prevent stack corruption)
    free(ctx);
}

static void tcpip_acd_stop_cb(void *arg) {
    (void)arg;
    acd_stop(&s_static_ip_acd);
}

#if !LWIP_ACD_RFC5227_COMPLIANT_STATIC
/* Legacy ACD function - only used when RFC 5227 compliant mode is disabled */
static bool tcpip_perform_acd(struct netif *netif, const ip4_addr_t *ip) {
    if (!g_tcpip.select_acd) {
        g_tcpip.status &= ~(kTcpipStatusAcdStatus | kTcpipStatusAcdFault);
        CipTcpIpSetLastAcdActivity(0);
        return true;
    }

    if (netif == NULL) {
        ESP_LOGW(TAG, "ACD requested but no netif available");
        g_tcpip.status |= kTcpipStatusAcdStatus | kTcpipStatusAcdFault;
        CipTcpIpSetLastAcdActivity(3);
        return false;
    }

    if (s_acd_sem == NULL) {
        s_acd_sem = xSemaphoreCreateBinary();
        if (s_acd_sem == NULL) {
            ESP_LOGE(TAG, "Failed to create ACD semaphore");
            g_tcpip.status |= kTcpipStatusAcdStatus | kTcpipStatusAcdFault;
            CipTcpIpSetLastAcdActivity(3);
            return false;
        }
    }

    while (xSemaphoreTake(s_acd_sem, 0) == pdTRUE) {
        /* flush any stale signals */
    }

    // Check if probe is still pending before creating context (prevents invalid context if cancelled)
    if (!s_acd_probe_pending) {
        ESP_LOGD(TAG, "tcpip_perform_acd: ACD probe no longer pending - skipping");
        return true;  // Return true to allow IP assignment (ACD was cancelled or completed)
    }

    // Initialize callback tracking: timeout without callback means probe sequence hasn't completed yet
    // Only explicit callback (ACD_IP_OK, ACD_RESTART_CLIENT, or ACD_DECLINE) indicates completion
    s_acd_callback_received = false;
    s_acd_last_state = ACD_IP_OK;  // Default assumption, but won't be used unless callback_received is true
    CipTcpIpSetLastAcdActivity(2);

    // Verify netif is still valid (may have been invalidated)
    if (netif == NULL) {
        ESP_LOGW(TAG, "tcpip_perform_acd: netif became NULL - ACD cancelled");
        return true;  // Return true to allow IP assignment (can't perform ACD without netif)
    }

    // Allocate context on heap: tcpip_callback_with_block executes asynchronously,
    // so stack-allocated context would be corrupted
    AcdStartContext *ctx = (AcdStartContext *)malloc(sizeof(AcdStartContext));
    if (ctx == NULL) {
        ESP_LOGE(TAG, "tcpip_perform_acd: Failed to allocate ACD context");
        g_tcpip.status |= kTcpipStatusAcdStatus | kTcpipStatusAcdFault;
        CipTcpIpSetLastAcdActivity(3);
        return false;
    }
    
    ctx->netif = netif;
    ctx->ip = *ip;
    ctx->err = ERR_OK;

    ESP_LOGD(TAG, "tcpip_perform_acd: Registering ACD client for IP " IPSTR, IP2STR(ip));
    
    // Create registration semaphore to wait for callback to complete registration
    if (s_acd_registration_sem == NULL) {
        s_acd_registration_sem = xSemaphoreCreateBinary();
        if (s_acd_registration_sem == NULL) {
            ESP_LOGE(TAG, "Failed to create ACD registration semaphore");
            free(ctx);
            g_tcpip.status |= kTcpipStatusAcdStatus | kTcpipStatusAcdFault;
            CipTcpIpSetLastAcdActivity(3);
            return false;
        }
    }
    
    // Clear any stale signals
    while (xSemaphoreTake(s_acd_registration_sem, 0) == pdTRUE) {
        /* flush any stale signals */
    }
    
    // Try direct registration first (faster), fallback to callback if needed
    if (!s_acd_registered) {
        ESP_LOGD(TAG, "tcpip_perform_acd: Attempting direct ACD registration");
        netif->acd_list = NULL;
        memset(&s_static_ip_acd, 0, sizeof(s_static_ip_acd));
        err_t add_err = acd_add(netif, &s_static_ip_acd, tcpip_acd_conflict_callback);
        if (add_err == ERR_OK) {
            s_acd_registered = true;
            ESP_LOGD(TAG, "tcpip_perform_acd: Direct ACD registration succeeded");
            free(ctx);  // Free context since we didn't use callback
        } else {
            ESP_LOGW(TAG, "tcpip_perform_acd: Direct registration failed (err=%d), trying callback", (int)add_err);
            // Fall through to callback method
        }
    }
    
    // If direct registration failed, try via callback (ensures thread safety)
    if (!s_acd_registered) {
        ESP_LOGD(TAG, "tcpip_perform_acd: Registering ACD client via callback");
        err_t callback_err = tcpip_callback_with_block(tcpip_acd_start_cb, ctx, 1);
        // ctx is now freed by the callback
        
        if (callback_err != ERR_OK) {
            ESP_LOGE(TAG, "Failed to register ACD client (callback_err=%d)", (int)callback_err);
            g_tcpip.status |= kTcpipStatusAcdStatus | kTcpipStatusAcdFault;
            CipTcpIpSetLastAcdActivity(3);
            return false;
        }
        
        // Wait for registration callback to complete (ensures s_acd_registered is set)
        TickType_t registration_timeout = pdMS_TO_TICKS(500);  // 500ms timeout
        if (xSemaphoreTake(s_acd_registration_sem, registration_timeout) != pdTRUE) {
            ESP_LOGW(TAG, "ACD registration callback timed out - trying direct registration as fallback");
            // Last resort: try direct registration again
            if (!s_acd_registered) {
                netif->acd_list = NULL;
                memset(&s_static_ip_acd, 0, sizeof(s_static_ip_acd));
                err_t add_err = acd_add(netif, &s_static_ip_acd, tcpip_acd_conflict_callback);
                if (add_err == ERR_OK) {
                    s_acd_registered = true;
                    ESP_LOGI(TAG, "tcpip_perform_acd: Fallback direct registration succeeded");
                } else {
                    ESP_LOGE(TAG, "ACD registration failed via both callback and direct methods");
                    g_tcpip.status |= kTcpipStatusAcdStatus | kTcpipStatusAcdFault;
                    CipTcpIpSetLastAcdActivity(3);
                    return false;
                }
            }
        }
        
        if (!s_acd_registered) {
            ESP_LOGE(TAG, "ACD registration callback completed but registration failed");
            g_tcpip.status |= kTcpipStatusAcdStatus | kTcpipStatusAcdFault;
            CipTcpIpSetLastAcdActivity(3);
            return false;
        }
    }
    
    // Start ACD probe directly (we're on tcpip thread or direct registration succeeded)
    if (s_acd_probe_pending && s_acd_registered) {
        ESP_LOGD(TAG, "tcpip_perform_acd: Starting ACD probe for IP " IPSTR, IP2STR(ip));
        err_t acd_start_err = acd_start(netif, &s_static_ip_acd, *ip);
        if (acd_start_err == ERR_OK) {
            ESP_LOGD(TAG, "tcpip_perform_acd: ACD probe started");
        } else {
            ESP_LOGE(TAG, "tcpip_perform_acd: acd_start() failed with err=%d", (int)acd_start_err);
            // Try via callback as fallback
            AcdStartProbeContext *probe_ctx = (AcdStartProbeContext *)malloc(sizeof(AcdStartProbeContext));
            if (probe_ctx == NULL) {
                ESP_LOGE(TAG, "Failed to allocate probe context");
                g_tcpip.status |= kTcpipStatusAcdStatus | kTcpipStatusAcdFault;
                CipTcpIpSetLastAcdActivity(3);
                return false;
            }
            
            probe_ctx->netif = netif;
            probe_ctx->ip = *ip;
            probe_ctx->err = ERR_OK;
            
            err_t callback_err = tcpip_callback_with_block(acd_start_probe_cb, probe_ctx, 1);
            if (callback_err != ERR_OK) {
                ESP_LOGE(TAG, "tcpip_perform_acd: acd_start() callback failed (callback_err=%d)", 
                         (int)callback_err);
                free(probe_ctx);
                g_tcpip.status |= kTcpipStatusAcdStatus | kTcpipStatusAcdFault;
                CipTcpIpSetLastAcdActivity(3);
                return false;
            }
            ESP_LOGI(TAG, "tcpip_perform_acd: ACD probe started via callback");
        }
    } else {
        ESP_LOGW(TAG, "tcpip_perform_acd: Cannot start ACD probe - probe_pending=%d, registered=%d", 
                 s_acd_probe_pending, s_acd_registered);
    }

    // Wait for ACD to complete - probe phase takes ~600-800ms (3 probes × 200ms + wait times)
    // Announce phase takes ~8s (4 announcements × 2s), but we can assign IP after probes complete
    // Total probe phase: PROBE_WAIT (0-200ms) + 3 probes (200ms each) + ANNOUNCE_WAIT (2000ms) = ~2.8-3s max
    // But we can assign after probe phase completes, so wait ~1.5s for probes + initial announce
    TickType_t wait_ticks = pdMS_TO_TICKS(2000);  // Increased from 500ms to allow full probe sequence

    ESP_LOGD(TAG, "Waiting for ACD probe sequence to complete (timeout: 2000ms)...");
    if (xSemaphoreTake(s_acd_sem, wait_ticks) == pdTRUE) {
        ESP_LOGI(TAG, "ACD completed with state=%d", (int)s_acd_last_state);
        if (s_acd_last_state == ACD_IP_OK) {
            CipTcpIpSetLastAcdActivity(0);
            return true;
        }
        // If we got a callback but not ACD_IP_OK, it might be a conflict
        if (s_acd_last_state == ACD_DECLINE || s_acd_last_state == ACD_RESTART_CLIENT) {
            ESP_LOGE(TAG, "ACD detected conflict (state=%d) - IP should not be assigned", (int)s_acd_last_state);
            CipTcpIpSetLastAcdActivity(3);
            return false;
        }
    } else if (s_acd_callback_received && s_acd_last_state == ACD_IP_OK) {
        // MODIFICATION: ACD callback was received but semaphore wait timed out
        // Added by: Adam G. Sweeney <agsweeney@gmail.com>
        // This is OK - callback set state to IP_OK, so we can safely continue
        // The timeout occurred because callback came after semaphore wait started,
        // but the state change confirms ACD completed successfully
        ESP_LOGI(TAG, "ACD callback received (state=IP_OK) - semaphore timeout was harmless, continuing with IP assignment");
        CipTcpIpSetLastAcdActivity(0);
        return true;
    }

    // Timeout - check if callback set conflict state during wait
    // Only explicit ACD_DECLINE/ACD_RESTART_CLIENT from callback indicates conflict
    // Timeout without callback means no conflict (s_acd_last_state remains ACD_IP_OK)
    if (s_acd_last_state == ACD_RESTART_CLIENT || s_acd_last_state == ACD_DECLINE) {
        // This state only occurs if callback was received, so it's a real conflict
        ESP_LOGE(TAG, "ACD conflict detected during probe phase (state=%d) - IP should not be assigned", (int)s_acd_last_state);
        CipTcpIpSetLastAcdActivity(3);
        tcpip_callback_with_block(tcpip_acd_stop_cb, NULL, 1);
        return false;
    }
    
    // Timeout without callback - ACD probe sequence is still in progress
    // Don't assign IP until callback confirms completion (ACD_IP_OK)
    // The callback will fire when announce phase completes (~6-10 seconds total)
    // Return true here to indicate "no conflict detected yet, waiting for callback"
    // This is different from returning false (which indicates actual conflict)
    ESP_LOGW(TAG, "ACD probe wait timed out (state=%d) - callback not received yet (probe sequence still running)", (int)s_acd_last_state);
    ESP_LOGW(TAG, "Note: ACD probe sequence can take 6-10 seconds (probes + announcements). Waiting for callback...");
    ESP_LOGW(TAG, "IP assignment will occur when ACD_IP_OK callback is received.");
    // Return true to indicate "no conflict, but waiting for callback to assign IP"
    // The callback will trigger IP assignment when it fires (see tcpip_acd_conflict_callback)
    return true;
}
#endif /* !LWIP_ACD_RFC5227_COMPLIANT_STATIC */

#if LWIP_ACD_RFC5227_COMPLIANT_STATIC
typedef struct {
    struct netif *netif;
    ip4_addr_t ip;
    ip4_addr_t netmask;
    ip4_addr_t gw;
    err_t err;
} Rfc5227AcdContext;

static void tcpip_rfc5227_acd_start_cb(void *arg) {
    Rfc5227AcdContext *ctx = (Rfc5227AcdContext *)arg;
    ESP_LOGI(TAG, "tcpip_rfc5227_acd_start_cb: Starting ACD for IP " IPSTR, IP2STR(&ctx->ip));
    ctx->err = netif_set_addr_with_acd(ctx->netif, &ctx->ip, &ctx->netmask, &ctx->gw, 
                                        tcpip_acd_conflict_callback);
    if (ctx->err == ERR_OK) {
        ESP_LOGI(TAG, "netif_set_addr_with_acd() succeeded - ACD probe sequence starting");
    } else {
        ESP_LOGE(TAG, "netif_set_addr_with_acd() failed with err=%d", (int)ctx->err);
    }
}
#endif

static void tcpip_try_pending_acd(esp_netif_t *netif, struct netif *lwip_netif) {
    ESP_LOGI(TAG, "tcpip_try_pending_acd: called - probe_pending=%d, netif=%p, lwip_netif=%p", 
             s_acd_probe_pending, netif, lwip_netif);
    if (!s_acd_probe_pending || netif == NULL || lwip_netif == NULL) {
        ESP_LOGW(TAG, "tcpip_try_pending_acd: Skipping - probe_pending=%d, netif=%p, lwip_netif=%p", 
                 s_acd_probe_pending, netif, lwip_netif);
        return;
    }
    if (!netif_has_valid_hwaddr(lwip_netif)) {
        ESP_LOGI(TAG, "ACD deferred until MAC address is available");
        return;
    }
    // Check if link is actually up - sometimes netif_is_link_up() can be delayed
    // Use a small delay to allow netif to fully initialize after ETHERNET_EVENT_CONNECTED
    if (!netif_is_link_up(lwip_netif)) {
        ESP_LOGI(TAG, "ACD deferred until link is up (link status: %d) - will retry", netif_is_link_up(lwip_netif));
        // Note: "invalid static ip" error from esp_netif_handlers is expected and harmless.
        // IP hasn't been assigned yet (waiting for ACD). Error disappears once IP is assigned.
        // Retry after short delay - link should be up soon after ETHERNET_EVENT_CONNECTED
        sys_timeout(100, tcpip_retry_acd_deferred, netif);
        return;
    }
    ESP_LOGI(TAG, "tcpip_try_pending_acd: All conditions met, starting ACD...");

#if LWIP_ACD_RFC5227_COMPLIANT_STATIC
    /* Use RFC 5227 compliant API: IP assignment deferred until ACD confirms */
    s_pending_esp_netif = netif;  /* Store for callback */
    Rfc5227AcdContext ctx = {
        .netif = lwip_netif,
        .ip.addr = s_pending_static_ip_cfg.ip.addr,
        .netmask.addr = s_pending_static_ip_cfg.netmask.addr,
        .gw.addr = s_pending_static_ip_cfg.gw.addr,
        .err = ERR_OK
    };
    
    CipTcpIpSetLastAcdActivity(2);
    ESP_LOGI(TAG, "Starting RFC 5227 ACD probe for IP " IPSTR, IP2STR(&ctx.ip));
    if (tcpip_callback_with_block(tcpip_rfc5227_acd_start_cb, &ctx, 1) != ERR_OK || ctx.err != ERR_OK) {
        ESP_LOGE(TAG, "Failed to start RFC 5227 compliant ACD (err=%d)", (int)ctx.err);
        CipTcpIpSetLastAcdActivity(3);
        g_tcpip.status |= kTcpipStatusAcdStatus | kTcpipStatusAcdFault;
        s_pending_esp_netif = NULL;
        /* Fall back to immediate assignment */
        ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &s_pending_static_ip_cfg));
        opener_configure_dns(netif);
        s_acd_probe_pending = false;
        CipTcpIpSetLastAcdActivity(0);
    } else {
        ESP_LOGI(TAG, "RFC 5227: ACD started for IP " IPSTR ", probing for conflicts...", IP2STR(&ctx.ip));
        ESP_LOGI(TAG, "ACD will send %d probes, waiting %d-%d ms between probes", 
                 CONFIG_OPENER_ACD_PROBE_NUM,
                 CONFIG_OPENER_ACD_PROBE_MIN_MS,
                 CONFIG_OPENER_ACD_PROBE_MAX_MS);
        /* IP will be assigned when ACD_IP_OK callback is received */
        /* DNS will be configured in the callback */
    }
#else
    /* Legacy ACD flow: Perform ACD BEFORE setting IP (better conflict detection) */
    ESP_LOGW(TAG, "Using legacy ACD mode - ACD runs before IP assignment");
    ip4_addr_t desired_ip = { .addr = s_pending_static_ip_cfg.ip.addr };
    CipTcpIpSetLastAcdActivity(2);
    ESP_LOGD(TAG, "Legacy ACD: Starting probe sequence for IP " IPSTR " BEFORE IP assignment", IP2STR(&desired_ip));
    
    // Run ACD BEFORE assigning IP to ensure conflicts are detected first
    (void)tcpip_perform_acd(lwip_netif, &desired_ip);
    
    // Check if callback was received and indicates conflict
    if (s_acd_callback_received && (s_acd_last_state == ACD_DECLINE || s_acd_last_state == ACD_RESTART_CLIENT)) {
        ESP_LOGE(TAG, "ACD conflict detected for " IPSTR " - NOT assigning IP", IP2STR(&desired_ip));
        ESP_LOGW(TAG, "IP assignment cancelled due to ACD conflict");
        g_tcpip.status |= kTcpipStatusAcdStatus | kTcpipStatusAcdFault;
        CipTcpIpSetLastAcdActivity(3);
        s_acd_probe_pending = false;
        // Stop ACD and cancel any pending retry timers
        tcpip_callback_with_block(tcpip_acd_stop_cb, NULL, 1);
        // Don't assign IP if conflict detected
        return;
    }
    
    // If callback was received with ACD_IP_OK, assign IP now
    // Otherwise (timeout without callback), IP will be assigned when callback fires
    if (s_acd_callback_received && s_acd_last_state == ACD_IP_OK) {
        ESP_LOGI(TAG, "Legacy ACD: No conflict detected - assigning IP " IPSTR, IP2STR(&desired_ip));
        ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &s_pending_static_ip_cfg));
        opener_configure_dns(netif);
        s_acd_probe_pending = false;
    } else {
        // Timeout without callback - probe sequence still running
        // IP will be assigned when ACD_IP_OK callback fires (see tcpip_acd_conflict_callback)
        ESP_LOGI(TAG, "Legacy ACD: Probe sequence in progress - IP will be assigned when callback fires");
    }
    
    // ACD_IP_OK callback fires AFTER announce phase completes, which means ACD is already in ONGOING state.
    // The ACD timer will naturally transition: PROBE_WAIT → PROBING → ANNOUNCE_WAIT → ANNOUNCING → ONGOING
    // So we don't need to manually transition - ACD is already in ONGOING state and will send periodic defensive ARPs.
    // Just set activity = 1 to indicate ongoing defense phase.
    CipTcpIpSetLastAcdActivity(1);
    ESP_LOGD(TAG, "Legacy ACD: ACD is in ONGOING state (callback fired after announce phase), periodic defense active");
    
    // Cancel any pending retry timers (retry handler checks s_acd_probe_pending and skips gracefully)
#endif
}

static void tcpip_retry_acd_deferred(void *arg) {
    esp_netif_t *netif = (esp_netif_t *)arg;
    if (netif == NULL) {
        ESP_LOGW(TAG, "tcpip_retry_acd_deferred: NULL netif - retry timer fired after cleanup");
        return;
    }
    
    // Check if probe is still pending (prevents retry after IP assignment or ACD completion)
    if (!s_acd_probe_pending) {
        ESP_LOGD(TAG, "tcpip_retry_acd_deferred: ACD probe no longer pending (IP likely assigned) - skipping retry");
        return;
    }
    
    struct netif *lwip_netif = (struct netif *)esp_netif_get_netif_impl(netif);
    if (lwip_netif != NULL) {
        ESP_LOGI(TAG, "tcpip_retry_acd_deferred: Retrying ACD start");
        tcpip_try_pending_acd(netif, lwip_netif);
    } else {
        ESP_LOGW(TAG, "tcpip_retry_acd_deferred: NULL lwip_netif - netif may not be fully initialized yet");
    }
}

#if CONFIG_OPENER_ACD_RETRY_ENABLED
/**
 * ACD Retry Logic
 * 
 * When a conflict is detected, removes IP address and schedules retry after delay.
 * Retry restarts the ACD probe sequence. Configurable max attempts and delay.
 */
static void tcpip_acd_retry_timer_callback(TimerHandle_t xTimer) {
    (void)xTimer;
    
    // Minimize stack usage: timer callbacks run in timer service task with limited stack
    // Set flag and let retry happen via tcpip callback (has more stack space)
    if (s_acd_retry_netif == NULL || s_acd_retry_lwip_netif == NULL) {
        return;  // Don't log - timer task has limited stack
    }
    
    // Reset probe pending flag to allow retry
    s_acd_probe_pending = true;
    
    // Execute retry on tcpip thread (has more stack space)
    err_t err = tcpip_callback_with_block(retry_callback, NULL, 0);
    if (err != ERR_OK) {
        // If callback fails, try direct call (may fail if not on tcpip thread, but won't crash)
        // Don't log here - timer task has limited stack
        tcpip_try_pending_acd(s_acd_retry_netif, s_acd_retry_lwip_netif);
    }
}

static void tcpip_acd_start_retry(esp_netif_t *netif, struct netif *lwip_netif) {
    if (netif == NULL || lwip_netif == NULL) {
        ESP_LOGE(TAG, "ACD retry: Invalid netif pointers");
        return;
    }
    
    // Increment retry count
    s_acd_retry_count++;
    
    // Store netif pointers for retry timer callback
    s_acd_retry_netif = netif;
    s_acd_retry_lwip_netif = lwip_netif;
    
    // Remove IP address (set to 0.0.0.0)
    esp_netif_ip_info_t zero_ip = {0};
    esp_err_t err = esp_netif_set_ip_info(netif, &zero_ip);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ACD retry: Failed to remove IP address: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "ACD retry: IP address removed (set to 0.0.0.0)");
    }
    
    // Stop ACD monitoring
    if (s_acd_registered) {
        acd_stop(&s_static_ip_acd);
        s_acd_registered = false;
    }
    
    // Create retry timer if it doesn't exist
    if (s_acd_retry_timer == NULL) {
        s_acd_retry_timer = xTimerCreate(
            "acd_retry",
            pdMS_TO_TICKS(CONFIG_OPENER_ACD_RETRY_DELAY_MS),
            pdFALSE,  // One-shot timer
            NULL,
            tcpip_acd_retry_timer_callback
        );
        
        if (s_acd_retry_timer == NULL) {
            ESP_LOGE(TAG, "ACD retry: Failed to create retry timer");
            return;
        }
    }
    
    // Reset timer to delay value and start it
    xTimerChangePeriod(s_acd_retry_timer, 
                       pdMS_TO_TICKS(CONFIG_OPENER_ACD_RETRY_DELAY_MS),
                       portMAX_DELAY);
    xTimerStart(s_acd_retry_timer, portMAX_DELAY);
    
    ESP_LOGI(TAG, "ACD retry: Timer started - will retry in %dms", CONFIG_OPENER_ACD_RETRY_DELAY_MS);
}
#endif /* CONFIG_OPENER_ACD_RETRY_ENABLED */

#if !LWIP_IPV4 || !LWIP_ACD
// Stub implementation when ACD is not available
static bool tcpip_perform_acd(struct netif *netif, const ip4_addr_t *ip) {
    (void)netif;
    (void)ip;
    if (g_tcpip.select_acd) {
        ESP_LOGW(TAG, "ACD requested but not supported by lwIP configuration");
    }
    g_tcpip.status &= ~(kTcpipStatusAcdStatus | kTcpipStatusAcdFault);
    return true;
}
#endif /* !LWIP_IPV4 || !LWIP_ACD */

static void configure_netif_from_tcpip(esp_netif_t *netif) {
    if (netif == NULL) {
        return;
    }

    struct netif *lwip_netif = (struct netif *)esp_netif_get_netif_impl(netif);

    if (tcpip_config_uses_dhcp()) {
        esp_netif_dhcpc_stop(netif);
        esp_netif_dhcpc_start(netif);
    } else {
        esp_netif_ip_info_t ip_info = {0};
        ip_info.ip.addr = g_tcpip.interface_configuration.ip_address;
        ip_info.netmask.addr = g_tcpip.interface_configuration.network_mask;
        ip_info.gw.addr = g_tcpip.interface_configuration.gateway;
        esp_netif_dhcpc_stop(netif);

        if (ip_info_has_static_address(&ip_info)) {
#if LWIP_ACD_RFC5227_COMPLIANT_STATIC
            /* RFC 5227 mode: Don't set IP immediately if ACD is enabled */
            if (!g_tcpip.select_acd) {
                /* ACD disabled, set IP immediately */
                ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip_info));
            }
            /* If ACD enabled, IP will be set via netif_set_addr_with_acd() */
#else
            /* Legacy ACD mode: Check if ACD is enabled BEFORE setting IP */
            if (g_tcpip.select_acd) {
                /* ACD enabled - defer IP assignment until ACD completes */
                /* IP will be set after ACD probe completes (see tcpip_try_pending_acd) */
                ESP_LOGI(TAG, "Legacy ACD enabled - IP assignment deferred until ACD completes");
            } else {
                /* ACD disabled - set IP immediately */
                ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip_info));
            }
#endif
        } else {
            ESP_LOGW(TAG, "Static configuration missing IP/mask; attempting AutoIP fallback");
#if CONFIG_LWIP_AUTOIP
            if (lwip_netif != NULL && netifapi_autoip_start(lwip_netif) == ERR_OK) {
                ESP_LOGI(TAG, "AutoIP started successfully");
                g_tcpip.config_control &= ~kTcpipCfgCtrlMethodMask;
                g_tcpip.config_control |= kTcpipCfgCtrlDhcp;
                g_tcpip.interface_configuration.ip_address = 0;
                g_tcpip.interface_configuration.network_mask = 0;
                g_tcpip.interface_configuration.gateway = 0;
                g_tcpip.interface_configuration.name_server = 0;
                g_tcpip.interface_configuration.name_server_2 = 0;
                NvTcpipStore(&g_tcpip);
                return;
            }
            ESP_LOGE(TAG, "AutoIP start failed; falling back to DHCP");
#endif
            ESP_LOGW(TAG, "Switching interface to DHCP due to invalid static configuration");
            g_tcpip.config_control &= ~kTcpipCfgCtrlMethodMask;
            g_tcpip.config_control |= kTcpipCfgCtrlDhcp;
            NvTcpipStore(&g_tcpip);
            ESP_ERROR_CHECK(esp_netif_dhcpc_start(netif));
            return;
        }

#if LWIP_IPV4 && LWIP_ACD
        if (g_tcpip.select_acd) {
            /* ACD enabled - use deferred assignment */
            s_pending_static_ip_cfg = ip_info;
            s_acd_probe_pending = true;
            CipTcpIpSetLastAcdActivity(1);
            ESP_LOGI(TAG, "ACD path: select_acd=%d, RFC5227=%d, lwip_netif=%p", 
                     g_tcpip.select_acd ? 1 : 0,
#if LWIP_ACD_RFC5227_COMPLIANT_STATIC
                     1,
#else
                     0,
#endif
                     (void *)lwip_netif);
            if (lwip_netif != NULL) {
#if LWIP_ACD_RFC5227_COMPLIANT_STATIC
                ESP_LOGI(TAG, "Using RFC 5227 compliant ACD for static IP");
#else
                ESP_LOGI(TAG, "Using legacy ACD for static IP");
#endif
                tcpip_try_pending_acd(netif, lwip_netif);
            }
        } else {
            /* ACD disabled - set IP immediately */
            CipTcpIpSetLastAcdActivity(0);
            s_acd_probe_pending = false;
            ESP_LOGI(TAG, "ACD disabled - setting static IP immediately");
            ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip_info));
            opener_configure_dns(netif);
        }
#else
        ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip_info));
        opener_configure_dns(netif);
#endif
    }

    configure_hostname(netif);
    g_tcpip.status |= 0x01;
    g_tcpip.status &= ~kTcpipStatusIfaceCfgPend;
}

static void ethernet_event_handler(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;
    esp_netif_t *eth_netif = (esp_netif_t *)arg;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "Ethernet Link Up");
        ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
               mac_addr[0], mac_addr[1], mac_addr[2],
               mac_addr[3], mac_addr[4], mac_addr[5]);
        ESP_ERROR_CHECK(esp_netif_set_mac(eth_netif, mac_addr));
        #if LWIP_IPV4 && LWIP_ACD
        if (!tcpip_config_uses_dhcp()) {
            struct netif *lwip_netif = (struct netif *)esp_netif_get_netif_impl(eth_netif);
            tcpip_try_pending_acd(eth_netif, lwip_netif);
            sys_timeout(200, tcpip_retry_acd_deferred, eth_netif);
        }
        #endif
        SampleApplicationNotifyLinkUp();
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Down");
        #if LWIP_IPV4 && LWIP_ACD
        tcpip_callback_with_block(tcpip_acd_stop_cb, NULL, 1);
        #endif
        s_opener_initialized = false;
        s_services_initialized = false;  // Allow re-initialization when link comes back up
        SampleApplicationNotifyLinkDown();
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet Stopped");
        break;
    default:
        break;
    }
}

static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;
    
    ESP_LOGI(TAG, "Ethernet Got IP Address");
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    ESP_LOGI(TAG, "IP Address: " IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    
    // Create mutex on first call if needed
    if (s_netif_mutex == NULL) {
        s_netif_mutex = xSemaphoreCreateMutex();
        if (s_netif_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create netif mutex");
            return;
        }
    }
    
    // Take mutex to protect s_netif access
    if (xSemaphoreTake(s_netif_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take netif mutex");
        return;
    }
    
    if (s_netif == NULL) {
        for (struct netif *netif = netif_list; netif != NULL; netif = netif->next) {
            if (netif_is_up(netif) && netif_is_link_up(netif)) {
                s_netif = netif;
                break;
            }
        }
    }
    
    struct netif *netif_to_use = s_netif;
    xSemaphoreGive(s_netif_mutex);
    
    if (netif_to_use != NULL) {
        SampleApplicationSetActiveNetif(netif_to_use);
        
        // Initialize services only once (IP_EVENT_ETH_GOT_IP can fire multiple times)
        if (!s_services_initialized) {
            opener_init(netif_to_use);
            s_opener_initialized = true;
            SampleApplicationNotifyLinkUp();
            
            // Initialize OTA manager
            if (!ota_manager_init()) {
                ESP_LOGW(TAG, "Failed to initialize OTA manager");
            }
            
            // Initialize and start Web UI
            if (!webui_init()) {
                ESP_LOGW(TAG, "Failed to initialize Web UI");
            }
            
            // ModbusTCP is always enabled
            if (!modbus_tcp_init()) {
                ESP_LOGW(TAG, "Failed to initialize ModbusTCP");
            } else {
                if (!modbus_tcp_start()) {
                    ESP_LOGW(TAG, "Failed to start ModbusTCP server");
                } else {
                    ESP_LOGI(TAG, "ModbusTCP server started");
                }
            }
            
            // Perform comprehensive I2C bus scan to identify all connected devices
            if (s_i2c_bus_handle != NULL) {
                ESP_LOGI(TAG, "=== I2C Bus Scan ===");
                scan_i2c_bus(s_i2c_bus_handle);
                ESP_LOGI(TAG, "=== End I2C Bus Scan ===");
            } else {
                ESP_LOGW(TAG, "I2C bus not available for scanning");
            }
            
            // Start IMU test task (always try to initialize if I2C bus is available)
            // Will try MPU6050 first, then fall back to LSM6DS3 if MPU6050 not detected
            if (s_imu_test_task_handle == NULL) {
                if (s_i2c_bus_handle != NULL) {
                    BaseType_t result = xTaskCreatePinnedToCore(imu_test_task,
                                                               "IMU_TEST",
                                                               4096,  // Stack size
                                                               NULL,
                                                               4,     // Priority (lower than MCP I/O)
                                                               &s_imu_test_task_handle,
                                                               1);    // Core 1
                    if (result == pdPASS) {
                        ESP_LOGI(TAG, "IMU test task started on Core 1");
                    } else {
                        ESP_LOGW(TAG, "Failed to create IMU test task");
                    }
                } else {
                    ESP_LOGI(TAG, "IMU test task skipped - I2C bus not available");
                }
            }
            
            s_services_initialized = true;
            ESP_LOGI(TAG, "All services initialized");
        } else {
            ESP_LOGD(TAG, "Services already initialized, skipping re-initialization");
        }
    } else {
        ESP_LOGE(TAG, "Failed to find netif");
    }
}

void app_main(void)
{
    // Initialize user LED early at boot
    user_led_init();
    
    // Initialize log buffer early to capture boot logs
    // Use 32KB buffer to capture boot sequence and recent runtime logs
    if (!log_buffer_init(32 * 1024)) {
        ESP_LOGW(TAG, "Failed to initialize log buffer");
    }
    
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);
    
    // Mark the current running app as valid to allow OTA updates
    // This must be done after NVS init and before any OTA operations
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running != NULL) {
        esp_ota_img_states_t ota_state;
        if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
            if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
                ESP_LOGI(TAG, "Marking OTA image as valid");
                esp_err_t ret = esp_ota_mark_app_valid_cancel_rollback();
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to mark app as valid: %s", esp_err_to_name(ret));
                }
            }
        }
    }
    
    (void)NvTcpipLoad(&g_tcpip);
    ESP_LOGI(TAG, "After NV load select_acd=%d", g_tcpip.select_acd);
    
    // Ensure ACD is enabled for static IP configuration
    if (!tcpip_config_uses_dhcp() && !g_tcpip.select_acd) {
        ESP_LOGW(TAG, "ACD not enabled for static IP - enabling ACD for conflict detection");
        g_tcpip.select_acd = true;
        NvTcpipStore(&g_tcpip);
        ESP_LOGI(TAG, "ACD enabled successfully");
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Ensure default configuration uses DHCP when nothing stored */
    if ((g_tcpip.config_control & kTcpipCfgCtrlMethodMask) != kTcpipCfgCtrlStaticIp &&
        (g_tcpip.config_control & kTcpipCfgCtrlMethodMask) != kTcpipCfgCtrlDhcp) {
        g_tcpip.config_control &= ~kTcpipCfgCtrlMethodMask;
        g_tcpip.config_control |= kTcpipCfgCtrlDhcp;
    }
    if (!tcpip_static_config_valid()) {
        ESP_LOGW(TAG, "Invalid static configuration detected, switching to DHCP");
        g_tcpip.config_control &= ~kTcpipCfgCtrlMethodMask;
        g_tcpip.config_control |= kTcpipCfgCtrlDhcp;
        g_tcpip.interface_configuration.ip_address = 0;
        g_tcpip.interface_configuration.network_mask = 0;
        g_tcpip.interface_configuration.gateway = 0;
        g_tcpip.interface_configuration.name_server = 0;
        g_tcpip.interface_configuration.name_server_2 = 0;
        g_tcpip.status &= ~(kTcpipStatusAcdStatus | kTcpipStatusAcdFault);
        NvTcpipStore(&g_tcpip);
    }
    /* if (g_tcpip.select_acd) {
        ESP_LOGW(TAG, "ACD selection stored in NV; disabling at boot");
        g_tcpip.select_acd = false;
        g_tcpip.status &= ~(kTcpipStatusAcdStatus | kTcpipStatusAcdFault);
        NvTcpipStore(&g_tcpip);
    } */
    if (tcpip_config_uses_dhcp()) {
        g_tcpip.interface_configuration.ip_address = 0;
        g_tcpip.interface_configuration.network_mask = 0;
        g_tcpip.interface_configuration.gateway = 0;
        g_tcpip.interface_configuration.name_server = 0;
        g_tcpip.interface_configuration.name_server_2 = 0;
    }

    g_tcpip.status |= 0x01;
    g_tcpip.status &= ~kTcpipStatusIfaceCfgPend;

    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&cfg);
    ESP_ERROR_CHECK(esp_netif_set_default_netif(eth_netif));

    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, 
                                               &ethernet_event_handler, eth_netif));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, 
                                               &got_ip_event_handler, eth_netif));

    eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    
    phy_config.phy_addr = CONFIG_OPENER_ETH_PHY_ADDR;
    phy_config.reset_gpio_num = CONFIG_OPENER_ETH_PHY_RST_GPIO;

    esp32_emac_config.smi_gpio.mdc_num = CONFIG_OPENER_ETH_MDC_GPIO;
    esp32_emac_config.smi_gpio.mdio_num = CONFIG_OPENER_ETH_MDIO_GPIO;

    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_config);

    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    ESP_ERROR_CHECK(esp_eth_driver_install(&config, &eth_handle));

    esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(eth_handle);
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, glue));
    
    // Initialize I2C bus for MPU6050 and other I2C devices
    i2c_master_bus_config_t i2c_bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = CONFIG_OPENER_I2C_SDA_GPIO,
        .scl_io_num = CONFIG_OPENER_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = system_i2c_internal_pullup_load(),
        },
    };
    
    esp_err_t i2c_err = i2c_new_master_bus(&i2c_bus_config, &s_i2c_bus_handle);
    if (i2c_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C bus: %s", esp_err_to_name(i2c_err));
        s_i2c_bus_handle = NULL;
    } else {
        ESP_LOGI(TAG, "I2C bus initialized successfully (SCL: GPIO%d, SDA: GPIO%d)", 
                 CONFIG_OPENER_I2C_SCL_GPIO, CONFIG_OPENER_I2C_SDA_GPIO);
    }

    configure_netif_from_tcpip(eth_netif);
    
    ESP_ERROR_CHECK(esp_eth_start(eth_handle));
}

// Comprehensive I2C bus scan function
static void scan_i2c_bus(i2c_master_bus_handle_t bus_handle)
{
    if (bus_handle == NULL) {
        ESP_LOGW(TAG, "I2C bus scan: bus handle is NULL");
        return;
    }
    
    ESP_LOGI(TAG, "Scanning I2C bus for devices...");
    
    uint8_t found_addresses[128] = {0};
    int found_count = 0;
    
    // Scan addresses 0x08 to 0x77 (valid I2C address range, excluding reserved addresses)
    // Reserved addresses: 0x00-0x07 (general call, reserved), 0x78-0x7F (reserved)
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        esp_err_t err = i2c_master_probe(bus_handle, addr, 100);  // Short timeout for scan
        if (err == ESP_OK) {
            found_addresses[found_count++] = addr;
        }
        // Small delay to avoid bus congestion
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    if (found_count == 0) {
        ESP_LOGW(TAG, "I2C bus scan: No devices found");
        return;
    }
    
    ESP_LOGI(TAG, "I2C bus scan: Found %d device(s):", found_count);
    
    // Print found devices in a formatted table
    ESP_LOGI(TAG, "┌─────────┬─────────────────────────────────────┐");
    ESP_LOGI(TAG, "│ Address │ Device Type                         │");
    ESP_LOGI(TAG, "├─────────┼─────────────────────────────────────┤");
    
    for (int i = 0; i < found_count; i++) {
        uint8_t addr = found_addresses[i];
        const char* device_name = "Unknown device";
        
        switch (addr) {
            case 0x29: 
                device_name = "Unknown device";
                break;
            case 0x2A: 
                break;
            case 0x68: 
                device_name = "MPU6050 (IMU) - AD0 LOW";
                break;
            case 0x69: 
                device_name = "MPU6050 (IMU) - AD0 HIGH";
                break;
            default:
                device_name = "Unknown device";
                break;
        }
        
        ESP_LOGI(TAG, "│  0x%02X   │ %-35s │", addr, device_name);
    }
    
    ESP_LOGI(TAG, "└─────────┴─────────────────────────────────────┘");
    
    // Additional info for known devices
    bool has_mpu6050 = false;
    
    for (int i = 0; i < found_count; i++) {
        uint8_t addr = found_addresses[i];
        if (addr == 0x68 || addr == 0x69) has_mpu6050 = true;
    }
    
    ESP_LOGI(TAG, "Device summary:");
    if (has_mpu6050) ESP_LOGI(TAG, "  ✓ MPU6050 IMU detected");
}

// User LED control functions
static void user_led_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << USER_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret == ESP_OK) {
        s_user_led_initialized = true;
        // Start blinking by default at boot
        user_led_start_flash();
        ESP_LOGI(TAG, "User LED initialized on GPIO%d (blinking by default)", USER_LED_GPIO);
    } else {
        ESP_LOGE(TAG, "Failed to initialize user LED on GPIO%d: %s", USER_LED_GPIO, esp_err_to_name(ret));
    }
}

static void user_led_set(bool on) {
    if (s_user_led_initialized) {
        gpio_set_level(USER_LED_GPIO, on ? 1 : 0);
    }
}

static void user_led_flash_task(void *pvParameters) {
    (void)pvParameters;
    const TickType_t flash_interval = pdMS_TO_TICKS(500);  // 500ms on/off
    
    while (1) {
        if (s_user_led_flash_enabled) {
            user_led_set(true);
            vTaskDelay(flash_interval);
            user_led_set(false);
            vTaskDelay(flash_interval);
        } else {
            // If flashing disabled, keep LED on and exit task
            user_led_set(true);
            vTaskDelete(NULL);
            return;
        }
    }
}

static void user_led_start_flash(void) {
    if (!s_user_led_initialized) {
        return;
    }
    
    if (s_user_led_task_handle == NULL) {
        s_user_led_flash_enabled = true;
        BaseType_t ret = xTaskCreate(
            user_led_flash_task,
            "user_led_flash",
            2048,
            NULL,
            1,  // Low priority
            &s_user_led_task_handle
        );
        if (ret == pdPASS) {
            ESP_LOGI(TAG, "User LED: Started blinking (normal operation)");
        } else {
            ESP_LOGE(TAG, "Failed to create user LED flash task");
            s_user_led_flash_enabled = false;
        }
    }
}

static void user_led_stop_flash(void) {
    if (s_user_led_task_handle != NULL) {
        s_user_led_flash_enabled = false;
        // Wait a bit for the task to exit cleanly
        vTaskDelay(pdMS_TO_TICKS(100));
        if (s_user_led_task_handle != NULL) {
            s_user_led_task_handle = NULL;
            ESP_LOGI(TAG, "User LED: Stopped blinking (going solid for ACD conflict)");
        }
    }
}

// Helper function to try initializing MPU6050
static bool try_init_mpu6050(i2c_master_bus_handle_t bus_handle)
{
    // Try both I2C addresses (0x68 and 0x69) - AD0 pin can be LOW or HIGH
    uint8_t mpu6050_addr = MPU6050_I2C_ADDR_PRIMARY;  // Start with 0x68
    bool device_found = false;
    
    esp_err_t probe_err = i2c_master_probe(bus_handle, MPU6050_I2C_ADDR_PRIMARY, 1000);
    if (probe_err == ESP_OK) {
        mpu6050_addr = MPU6050_I2C_ADDR_PRIMARY;
        device_found = true;
    } else {
        // Try secondary address
        probe_err = i2c_master_probe(bus_handle, MPU6050_I2C_ADDR_SECONDARY, 1000);
        if (probe_err == ESP_OK) {
            mpu6050_addr = MPU6050_I2C_ADDR_SECONDARY;
            device_found = true;
        }
    }
    
    if (!device_found) {
        ESP_LOGI(TAG, "MPU6050: Device not detected at either address (0x%02X or 0x%02X)", 
                 MPU6050_I2C_ADDR_PRIMARY, MPU6050_I2C_ADDR_SECONDARY);
        return false;
    }
    
    // Configure I2C device for MPU6050
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = mpu6050_addr,
        .scl_speed_hz = 400000,
    };
    
    esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_mpu6050_dev_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MPU6050: Failed to add I2C device: %s", esp_err_to_name(err));
        return false;
    }
    
    vTaskDelay(pdMS_TO_TICKS(200));  // Give I2C bus a moment to stabilize after adding device
    
    // Initialize MPU6050
    if (!mpu6050_init(&s_mpu6050, s_mpu6050_dev_handle)) {
        ESP_LOGE(TAG, "MPU6050: Failed to initialize device structure");
        i2c_master_bus_rm_device(s_mpu6050_dev_handle);
        s_mpu6050_dev_handle = NULL;
        return false;
    }
    
    // Read WHO_AM_I register to verify connection with retries
    uint8_t who_am_i = 0;
    bool who_am_i_success = false;
    esp_err_t last_err = ESP_OK;
    
    for (int retry = 0; retry < 10; retry++) {
        err = mpu6050_read_who_am_i(&s_mpu6050, &who_am_i);
        last_err = err;
        
        if (err == ESP_OK) {
            who_am_i_success = true;
            break;
        }
        
        // Retry on timeout, invalid state, or any I2C error
        if (err == ESP_ERR_TIMEOUT || err == ESP_ERR_INVALID_STATE || 
            err == ESP_ERR_INVALID_RESPONSE || err == ESP_FAIL) {
            int delay_ms = 200 * (retry + 1);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        } else {
            ESP_LOGE(TAG, "MPU6050: Unexpected error type, not retrying: %s", esp_err_to_name(err));
            break;
        }
    }
    
    if (!who_am_i_success) {
        ESP_LOGE(TAG, "MPU6050: Failed to read WHO_AM_I after %d attempts. Last error: %s", 
                 10, esp_err_to_name(last_err));
        i2c_master_bus_rm_device(s_mpu6050_dev_handle);
        s_mpu6050_dev_handle = NULL;
        return false;
    }
    
    if (who_am_i == MPU6050_WHO_AM_I_VALUE) {
        ESP_LOGI(TAG, "MPU6050: Standard chip detected (WHO_AM_I: 0x%02X)", who_am_i);
    } else if (who_am_i == 0x98) {
        ESP_LOGI(TAG, "MPU6050: Clone chip detected (WHO_AM_I: 0x%02X) - continuing with initialization", who_am_i);
    } else {
        ESP_LOGW(TAG, "MPU6050: Unexpected WHO_AM_I value: 0x%02X (expected 0x%02X or 0x98 for clone)", 
                 who_am_i, MPU6050_WHO_AM_I_VALUE);
    }
    
    // Reset and wake up the device
    err = mpu6050_reset(&s_mpu6050);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MPU6050: Reset failed: %s (continuing anyway)", esp_err_to_name(err));
    }
    
    vTaskDelay(pdMS_TO_TICKS(100));  // Wait for reset to complete
    
    err = mpu6050_wake_up(&s_mpu6050);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MPU6050: Wake up failed: %s (continuing anyway)", esp_err_to_name(err));
    }
    
    // Configure with defaults
    err = mpu6050_configure_default(&s_mpu6050);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MPU6050: Default configuration failed: %s (continuing anyway)", esp_err_to_name(err));
    }
    
    s_mpu6050_initialized = true;
    s_active_imu_type = IMU_TYPE_MPU6050;
    ESP_LOGI(TAG, "MPU6050: Successfully initialized");
    return true;
}

// Helper function to try initializing LSM6DS3
static bool try_init_lsm6ds3(i2c_master_bus_handle_t bus_handle)
{
    // Try both I2C addresses (0x6A and 0x6B) - SA0 pin can be LOW or HIGH
    uint8_t lsm6ds3_addr = 0x6A;  // Default address (SA0 LOW)
    bool device_found = false;
    
    esp_err_t probe_err = i2c_master_probe(bus_handle, 0x6A, 1000);
    if (probe_err == ESP_OK) {
        lsm6ds3_addr = 0x6A;
        device_found = true;
    } else {
        // Try secondary address
        probe_err = i2c_master_probe(bus_handle, 0x6B, 1000);
        if (probe_err == ESP_OK) {
            lsm6ds3_addr = 0x6B;
            device_found = true;
        }
    }
    
    if (!device_found) {
        ESP_LOGI(TAG, "LSM6DS3: Device not detected at either address (0x6A or 0x6B)");
        return false;
    }
    
    // Configure LSM6DS3
    lsm6ds3_config_t config = {
        .interface = LSM6DS3_INTERFACE_I2C,
        .bus.i2c.bus_handle = bus_handle,
        .bus.i2c.address = lsm6ds3_addr,
    };
    
    esp_err_t err = lsm6ds3_init(&s_lsm6ds3_handle, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LSM6DS3: Failed to initialize: %s", esp_err_to_name(err));
        return false;
    }
    
    // Verify device ID
    uint8_t device_id = 0;
    err = lsm6ds3_get_device_id(&s_lsm6ds3_handle, &device_id);
    if (err != ESP_OK || device_id != LSM6DS3_ID) {
        ESP_LOGE(TAG, "LSM6DS3: Invalid device ID: 0x%02X (expected 0x%02X)", device_id, LSM6DS3_ID);
        lsm6ds3_deinit(&s_lsm6ds3_handle);
        return false;
    }
    
    ESP_LOGI(TAG, "LSM6DS3: Device detected (ID: 0x%02X)", device_id);
    
    // Reset sensor to ensure clean state
    ESP_LOGI(TAG, "LSM6DS3: Resetting sensor...");
    err = lsm6ds3_reset(&s_lsm6ds3_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LSM6DS3: Failed to reset sensor: %s (continuing anyway)", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "LSM6DS3: Sensor reset complete");
        vTaskDelay(pdMS_TO_TICKS(50));  // Wait for reset to complete
    }
    
    // Configure sensor settings
    err = lsm6ds3_set_accel_odr(&s_lsm6ds3_handle, LSM6DS3_XL_ODR_104Hz);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LSM6DS3: Failed to set accelerometer ODR: %s (continuing anyway)", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "LSM6DS3: Accelerometer ODR set to 104Hz");
    }
    
    err = lsm6ds3_set_accel_full_scale(&s_lsm6ds3_handle, LSM6DS3_2g);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LSM6DS3: Failed to set accelerometer full scale: %s (continuing anyway)", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "LSM6DS3: Accelerometer full scale set to ±2g");
    }
    
    err = lsm6ds3_set_gyro_odr(&s_lsm6ds3_handle, LSM6DS3_GY_ODR_104Hz);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LSM6DS3: Failed to set gyroscope ODR: %s (continuing anyway)", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "LSM6DS3: Gyroscope ODR set to 104Hz");
    }
    
    err = lsm6ds3_set_gyro_full_scale(&s_lsm6ds3_handle, LSM6DS3_2000dps);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LSM6DS3: Failed to set gyroscope full scale: %s (continuing anyway)", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "LSM6DS3: Gyroscope full scale set to ±2000dps");
    }
    
    // Try disabling BDU first - it may be causing issues with continuous reads
    // BDU locks registers until all bytes are read, which can cause problems if we read too fast
    err = lsm6ds3_enable_block_data_update(&s_lsm6ds3_handle, false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LSM6DS3: Failed to disable BDU: %s (continuing anyway)", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "LSM6DS3: Block Data Update disabled (continuous mode)");
    }
    
    // Wait for sensor to stabilize after configuration
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Debug: Read back CTRL1_XL as raw byte to verify configuration
    uint8_t ctrl1_xl_raw = 0;
    if (s_lsm6ds3_handle.ctx.read_reg(s_lsm6ds3_handle.ctx.handle, 0x10, &ctrl1_xl_raw, 1) == 0) {
        ESP_LOGI(TAG, "LSM6DS3: CTRL1_XL raw=0x%02X (bits 7-4=ODR, bits 3-2=FS)", ctrl1_xl_raw);
        lsm6ds3_odr_xl_t odr_readback = (lsm6ds3_odr_xl_t)((ctrl1_xl_raw & 0xF0) >> 4);
        lsm6ds3_fs_xl_t fs_readback = (lsm6ds3_fs_xl_t)((ctrl1_xl_raw & 0x0C) >> 2);
        ESP_LOGI(TAG, "LSM6DS3: CTRL1_XL parsed - ODR=%d (expected 4=104Hz), FS=%d (expected 0=±2g)", 
                 odr_readback, fs_readback);
    }
    
    // Debug: Read CTRL3_C to check for any blocking conditions
    uint8_t ctrl3_c_raw = 0;
    if (s_lsm6ds3_handle.ctx.read_reg(s_lsm6ds3_handle.ctx.handle, 0x12, &ctrl3_c_raw, 1) == 0) {
        ESP_LOGI(TAG, "LSM6DS3: CTRL3_C raw=0x%02X", ctrl3_c_raw);
    }
    
    // Debug: Try reading a single accelerometer register to verify read path
    uint8_t outx_l_xl_raw = 0;
    if (s_lsm6ds3_handle.ctx.read_reg(s_lsm6ds3_handle.ctx.handle, 0x28, &outx_l_xl_raw, 1) == 0) {
        ESP_LOGI(TAG, "LSM6DS3: OUTX_L_XL (single byte) raw=0x%02X", outx_l_xl_raw);
    }
    
    // Debug: Check STATUS_REG to see if data is available
    lsm6ds3_status_reg_t status;
    if (lsm6ds3_status_reg_get(&s_lsm6ds3_handle.ctx, &status) == 0) {
        ESP_LOGI(TAG, "LSM6DS3: STATUS_REG - XLDA=%d GDA=%d TDA=%d", 
                 status.xlda, status.gda, status.tda);
    } else {
        ESP_LOGW(TAG, "LSM6DS3: Failed to read STATUS_REG");
    }
    
    // Try to load calibration from NVS first
    esp_err_t cal_load_err = lsm6ds3_load_calibration_from_nvs(&s_lsm6ds3_handle, "system");
    if (cal_load_err == ESP_OK && s_lsm6ds3_handle.calibration.gyro_calibrated) {
        ESP_LOGI(TAG, "LSM6DS3: Loaded gyroscope calibration from NVS");
    } else {
        // No calibration found in NVS, perform calibration
        // Calibrate gyroscope (device must be stationary during calibration)
        // This removes bias/drift that would cause angle accumulation errors
        ESP_LOGI(TAG, "LSM6DS3: Calibrating gyroscope (keep device still for 2 seconds)...");
        vTaskDelay(pdMS_TO_TICKS(500));  // Wait for sensor to stabilize
        err = lsm6ds3_calibrate_gyro(&s_lsm6ds3_handle, 100, 20);  // 100 samples at 20ms = 2 seconds
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "LSM6DS3: Failed to calibrate gyroscope: %s (continuing anyway)", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "LSM6DS3: Gyroscope calibration complete");
            // Save calibration to NVS for next boot
            esp_err_t cal_save_err = lsm6ds3_save_calibration_to_nvs(&s_lsm6ds3_handle, "system");
            if (cal_save_err != ESP_OK) {
                ESP_LOGW(TAG, "LSM6DS3: Failed to save calibration to NVS: %s", esp_err_to_name(cal_save_err));
            }
        }
    }
    
    // Initialize complementary filter for sensor fusion
    err = lsm6ds3_complementary_init(&s_lsm6ds3_filter, 0.96f, 104.0f);  // alpha=0.96, sample_rate=104Hz
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LSM6DS3: Failed to initialize complementary filter: %s (continuing anyway)", esp_err_to_name(err));
    }
    
    s_lsm6ds3_initialized = true;
    s_active_imu_type = IMU_TYPE_LSM6DS3;
    ESP_LOGI(TAG, "LSM6DS3: Successfully initialized");
    return true;
}

// Task to test IMU sensor - tries MPU6050 first, then falls back to LSM6DS3
static void imu_test_task(void *pvParameters)
{
    (void)pvParameters;
    
    ESP_LOGI(TAG, "IMU test task started - will try MPU6050 first, then LSM6DS3");
    
    // Initialize IMU state mutex
    imu_state_mutex_init();
    
    // Wait for I2C bus to be ready
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Wait for I2C bus to be initialized
    int retry_count = 0;
    while (s_i2c_bus_handle == NULL && retry_count < 10) {
        ESP_LOGW(TAG, "IMU: I2C bus handle not available, retrying... (%d/10)", retry_count + 1);
        vTaskDelay(pdMS_TO_TICKS(500));
        retry_count++;
    }
    
    if (s_i2c_bus_handle == NULL) {
        ESP_LOGE(TAG, "IMU: I2C bus handle is NULL after retries, cannot initialize");
        vTaskDelete(NULL);
        return;
    }
    
    i2c_master_bus_handle_t bus_handle = s_i2c_bus_handle;
    
    // Initialize IMU with mutex protection
    if (s_imu_state_mutex != NULL && xSemaphoreTake(s_imu_state_mutex, portMAX_DELAY) == pdTRUE) {
        // Try MPU6050 first
        ESP_LOGI(TAG, "IMU: Attempting to initialize MPU6050...");
        if (try_init_mpu6050(bus_handle)) {
            ESP_LOGI(TAG, "IMU: MPU6050 initialized successfully");
            s_active_imu_type = IMU_TYPE_MPU6050;
            // Load MPU6050 enabled state
            s_imu_enabled_cached = system_mpu6050_enabled_load();
        } else {
            // MPU6050 not found, try LSM6DS3
            ESP_LOGI(TAG, "IMU: MPU6050 not detected, trying LSM6DS3...");
            if (try_init_lsm6ds3(bus_handle)) {
                ESP_LOGI(TAG, "IMU: LSM6DS3 initialized successfully");
                s_active_imu_type = IMU_TYPE_LSM6DS3;
                // Load LSM6DS3 enabled state
                s_imu_enabled_cached = system_lsm6ds3_enabled_load();
            } else {
                ESP_LOGW(TAG, "IMU: Neither MPU6050 nor LSM6DS3 detected - no IMU available");
                s_active_imu_type = IMU_TYPE_NONE;
                xSemaphoreGive(s_imu_state_mutex);
                vTaskDelete(NULL);
                return;
            }
        }
        xSemaphoreGive(s_imu_state_mutex);
    } else {
        // Fallback: initialize without mutex (shouldn't happen)
        ESP_LOGW(TAG, "IMU: Mutex unavailable, initializing without protection");
        // Try MPU6050 first
        ESP_LOGI(TAG, "IMU: Attempting to initialize MPU6050...");
        if (try_init_mpu6050(bus_handle)) {
            ESP_LOGI(TAG, "IMU: MPU6050 initialized successfully");
            s_active_imu_type = IMU_TYPE_MPU6050;
            s_imu_enabled_cached = system_mpu6050_enabled_load();
        } else {
            // MPU6050 not found, try LSM6DS3
            ESP_LOGI(TAG, "IMU: MPU6050 not detected, trying LSM6DS3...");
            if (try_init_lsm6ds3(bus_handle)) {
                ESP_LOGI(TAG, "IMU: LSM6DS3 initialized successfully");
                s_active_imu_type = IMU_TYPE_LSM6DS3;
                s_imu_enabled_cached = system_lsm6ds3_enabled_load();
            } else {
                ESP_LOGW(TAG, "IMU: Neither MPU6050 nor LSM6DS3 detected - no IMU available");
                s_active_imu_type = IMU_TYPE_NONE;
                vTaskDelete(NULL);
                return;
            }
        }
    }
    
    // Always start I/O task if device is initialized (task checks enabled state)
    if (s_imu_io_task_handle == NULL) {
        BaseType_t result = xTaskCreatePinnedToCore(imu_io_task,
                                                   "IMU_IO",
                                                   4096,  // Stack size
                                                   NULL,
                                                   5,     // Priority (same as MCP I/O)
                                                   &s_imu_io_task_handle,
                                                   1);    // Core 1
        if (result == pdPASS) {
            // IMU I/O task started silently
        } else {
            ESP_LOGW(TAG, "Failed to create IMU I/O task");
        }
    }
    
    // Task complete - initialization done
    vTaskDelete(NULL);
}


// Task to continuously read IMU sensor data and write to Input Assembly 100
// Works with both MPU6050 and LSM6DS3 sensors
static void imu_io_task(void *pvParameters)
{
    (void)pvParameters;
    
    // Wait for initialization to complete (either MPU6050 or LSM6DS3)
    // Check with mutex protection
    imu_type_t active_type = IMU_TYPE_NONE;
    if (s_imu_state_mutex != NULL && xSemaphoreTake(s_imu_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        active_type = s_active_imu_type;
        xSemaphoreGive(s_imu_state_mutex);
    }
    
    while (active_type == IMU_TYPE_NONE) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (s_imu_state_mutex != NULL && xSemaphoreTake(s_imu_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            active_type = s_active_imu_type;
            xSemaphoreGive(s_imu_state_mutex);
        }
    }
    
    // Get assembly mutex
    SemaphoreHandle_t assembly_mutex = sample_application_get_assembly_mutex();
    
    // Get byte offset from NVS based on active sensor type (read with mutex)
    uint8_t byte_start;
    if (s_imu_state_mutex != NULL && xSemaphoreTake(s_imu_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        active_type = s_active_imu_type;
        xSemaphoreGive(s_imu_state_mutex);
    }
    
    if (active_type == IMU_TYPE_MPU6050) {
        byte_start = system_mpu6050_byte_start_load();
    } else if (active_type == IMU_TYPE_LSM6DS3) {
        byte_start = system_lsm6ds3_byte_start_load();
    } else {
        ESP_LOGE(TAG, "IMU I/O task: No active IMU type, cannot determine byte offset");
        vTaskDelete(NULL);
        return;
    }
    
    // Validate byte offset (IMU uses 20 bytes: 5 int32_t for roll, pitch, ground_angle, bottom_pressure, top_pressure)
    // Values are stored as scaled integers: degrees * 10000, pressure * 1000
    const uint8_t imu_data_size = 20;  // 5 int32_t * 4 bytes each
    if (byte_start + imu_data_size > sizeof(g_assembly_data064)) {
        ESP_LOGE(TAG, "IMU: Invalid byte offset %d (would exceed assembly size for %d bytes)", byte_start, imu_data_size);
        vTaskDelete(NULL);
        return;
    }
    
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period_ms = pdMS_TO_TICKS(20);  // 50 Hz update rate (faster updates)
    
    // Variables for sensor readings
    float roll = 0.0f, pitch = 0.0f, signed_ground_angle = 0.0f;
    
    while (1) {
        // Read enabled state and active IMU type with mutex protection
        bool imu_enabled = false;
        imu_type_t current_imu_type = IMU_TYPE_NONE;
        if (s_imu_state_mutex != NULL && xSemaphoreTake(s_imu_state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            imu_enabled = s_imu_enabled_cached;
            current_imu_type = s_active_imu_type;
            xSemaphoreGive(s_imu_state_mutex);
        } else {
            // Fallback: read without mutex (shouldn't happen, but safer)
            imu_enabled = s_imu_enabled_cached;
            current_imu_type = active_type;
        }
        
        // If disabled, zero out assembly bytes and skip reading
        if (!imu_enabled) {
            if (assembly_mutex != NULL) {
                xSemaphoreTake(assembly_mutex, portMAX_DELAY);
            }
            memset(&g_assembly_data064[byte_start], 0, imu_data_size);  // Clear all 20 bytes
            if (assembly_mutex != NULL) {
                xSemaphoreGive(assembly_mutex);
            }
            vTaskDelayUntil(&last_wake_time, period_ms);
            continue;
        }
        
        // Defensive check: ensure I2C bus handle is still valid
        if (s_i2c_bus_handle == NULL) {
            ESP_LOGE(TAG, "IMU I/O task: I2C bus handle became NULL, exiting");
            vTaskDelete(NULL);
            return;
        }
        
        // Read sensor data based on active sensor type
        esp_err_t accel_err = ESP_FAIL;
        esp_err_t gyro_err = ESP_FAIL;
        
        if (current_imu_type == IMU_TYPE_MPU6050) {
            // MPU6050 path
            mpu6050_accel_t accel;
            mpu6050_gyro_t gyro;
            accel_err = mpu6050_read_accel(&s_mpu6050, &accel);
            gyro_err = mpu6050_read_gyro(&s_mpu6050, &gyro);
            
            if (accel_err == ESP_OK && gyro_err == ESP_OK) {
                // Convert MPU6050 raw values to mg and mdps (for future use if needed)
                float accel_mg_local[3];
                float gyro_mdps_local[3];
                accel_mg_local[0] = (float)accel.x * s_mpu6050.accel_scale * 1000.0f;  // Convert g to mg
                accel_mg_local[1] = (float)accel.y * s_mpu6050.accel_scale * 1000.0f;
                accel_mg_local[2] = (float)accel.z * s_mpu6050.accel_scale * 1000.0f;
                gyro_mdps_local[0] = (float)gyro.x * s_mpu6050.gyro_scale * 1000.0f;  // Convert dps to mdps
                gyro_mdps_local[1] = (float)gyro.y * s_mpu6050.gyro_scale * 1000.0f;
                gyro_mdps_local[2] = (float)gyro.z * s_mpu6050.gyro_scale * 1000.0f;
                (void)accel_mg_local;  // Suppress unused warning
                (void)gyro_mdps_local;  // Suppress unused warning
                
                // Calculate orientation using MPU6050's built-in function
                mpu6050_sample_t sample = {0};
                sample.accel = accel;
                mpu6050_orientation_t orientation;
                esp_err_t err = mpu6050_calculate_orientation(&s_mpu6050, &sample.accel, &orientation);
                
                if (err == ESP_OK) {
                    roll = orientation.roll;
                    pitch = orientation.pitch;
                    // Calculate signed ground angle
                    if (orientation.abs_ground_angle > 0.0f) {
                        signed_ground_angle = (orientation.abs_ground_angle > 90.0f) 
                            ? -orientation.abs_ground_angle  // Past 90°: force reverses
                            : orientation.abs_ground_angle;  // 0-90°: normal direction
                    } else {
                        signed_ground_angle = 0.0f;
                    }
                } else {
                    ESP_LOGW(TAG, "IMU: Failed to calculate orientation from MPU6050");
                    vTaskDelayUntil(&last_wake_time, period_ms);
                    continue;
                }
            }
        } else if (current_imu_type == IMU_TYPE_LSM6DS3) {
            // LSM6DS3 path
            float accel_mg_temp[3];
            float gyro_mdps_temp[3];
            accel_err = lsm6ds3_read_accel(&s_lsm6ds3_handle, accel_mg_temp);
            gyro_err = lsm6ds3_read_gyro(&s_lsm6ds3_handle, gyro_mdps_temp);
            
            // Debug: Log read errors
            static uint32_t error_counter = 0;
            if (accel_err != ESP_OK || gyro_err != ESP_OK) {
                error_counter++;
                if (error_counter % 50 == 1) {  // Log every 50 errors to avoid spam
                    ESP_LOGW(TAG, "LSM6DS3: Read errors - Accel: %s, Gyro: %s (error #%lu)",
                             esp_err_to_name(accel_err), esp_err_to_name(gyro_err), error_counter);
                }
            }
            
            if (accel_err == ESP_OK && gyro_err == ESP_OK) {
                // Convert gyro from mdps to dps for fusion filter
                float gyro_dps[3];
                gyro_dps[0] = gyro_mdps_temp[0] / 1000.0f;  // mdps to dps
                gyro_dps[1] = gyro_mdps_temp[1] / 1000.0f;
                gyro_dps[2] = gyro_mdps_temp[2] / 1000.0f;
                
                // Convert accelerometer from mg to g for fusion filter
                float accel_g[3];
                accel_g[0] = accel_mg_temp[0] / 1000.0f;  // mg to g
                accel_g[1] = accel_mg_temp[1] / 1000.0f;
                accel_g[2] = accel_mg_temp[2] / 1000.0f;
                
                // Update complementary filter with accelerometer and gyroscope data
                // dt = period_ms / 1000.0f (20ms = 0.02s for 50Hz update rate)
                float dt = (float)period_ms / 1000.0f;
                esp_err_t fusion_err = lsm6ds3_complementary_update(&s_lsm6ds3_filter, accel_g, gyro_dps, dt);
                
                if (fusion_err == ESP_OK) {
                    // Get fused angles from complementary filter
                    lsm6ds3_complementary_get_angles(&s_lsm6ds3_filter, &roll, &pitch);
                    
                    // Calculate ground angle from vertical using fused roll and pitch
                    signed_ground_angle = lsm6ds3_calculate_angle_from_vertical(roll, pitch);
                    // Apply sign convention: negative for angles > 90°
                    if (signed_ground_angle > 90.0f) {
                        signed_ground_angle = -signed_ground_angle;
                    }
                    
                    // Sensor fusion running silently (no console logging)
                } else {
                    ESP_LOGW(TAG, "LSM6DS3: Complementary filter update failed: %s", esp_err_to_name(fusion_err));
                }
            }
        }
        
        if (accel_err != ESP_OK || gyro_err != ESP_OK) {
            ESP_LOGW(TAG, "IMU: Failed to read sensor data");
            vTaskDelayUntil(&last_wake_time, period_ms);
            continue;
        }
        
        // ============================================================================
        // SECTION 1: Calculate signed ground angle for display (already calculated above)
        // ============================================================================
            
            // ============================================================================
            // SECTION 2: Load configuration parameters
            // ============================================================================
            // Read tool weight, tip force, and cylinder bore from Output Assembly 150 (bytes 29, 30, 31)
            // Fall back to NVS if assembly bytes are 0
            float tool_weight_lbs = 50.0f;  // Default
            float desired_tip_force_lbs = 20.0f;  // Default
            float cylinder_bore_inches = 1.0f;  // Default
            
            if (assembly_mutex != NULL) {
                xSemaphoreTake(assembly_mutex, portMAX_DELAY);
            }
            
            if (sizeof(g_assembly_data096) >= 32) {
                // Read cylinder bore from byte 29 (scaled by 100: 0 = use NVS, 1-255 = 0.01-2.55 inches)
                uint8_t cylinder_bore_byte = g_assembly_data096[29];
                if (cylinder_bore_byte > 0 && cylinder_bore_byte <= 255) {
                    cylinder_bore_inches = (float)cylinder_bore_byte / 100.0f;  // Convert from scaled (1-255) to inches (0.01-2.55)
                } else {
                    // Fall back to NVS if byte is 0 or out of range
                    cylinder_bore_inches = system_cylinder_bore_load();
                }
                
                // Read tool weight from byte 30
                uint8_t tool_weight_byte = g_assembly_data096[30];
                tool_weight_lbs = (tool_weight_byte > 0) 
                    ? (float)tool_weight_byte 
                    : (float)system_tool_weight_load();
                
                // Read tip force from byte 31
                uint8_t tip_force_byte = g_assembly_data096[31];
                desired_tip_force_lbs = (tip_force_byte > 0) 
                    ? (float)tip_force_byte 
                    : (float)system_tip_force_load();
            } else {
                // Assembly too small, use NVS defaults
                cylinder_bore_inches = system_cylinder_bore_load();
                tool_weight_lbs = (float)system_tool_weight_load();
                desired_tip_force_lbs = (float)system_tip_force_load();
            }
            
            if (assembly_mutex != NULL) {
                xSemaphoreGive(assembly_mutex);
            }
            
            // Use same cylinder bore for both top and bottom cylinders
            const float bottom_cylinder_bore_inches = cylinder_bore_inches;
            const float top_cylinder_bore_inches = cylinder_bore_inches;
            
            // ============================================================================
            // SECTION 3: Sensor fusion - calculate absolute angle from vertical
            // ============================================================================
            // For MPU6050: Already calculated above using built-in orientation function
            // For LSM6DS3: Already calculated above using complementary filter
            // Both paths now have roll, pitch, and signed_ground_angle available
            
            // Calculate absolute ground angle from vertical for force calculations
            float abs_ground_angle_deg = fabsf(signed_ground_angle);
            float abs_ground_angle_rad = abs_ground_angle_deg * M_PI / 180.0f;
            
            // Calculate gravity component along slide axis
            float cos_angle_from_vertical = cosf(abs_ground_angle_rad);
            float gravity_component_lbs = tool_weight_lbs * cos_angle_from_vertical;
            
            // ============================================================================
            // SECTION 4: Calculate cylinder forces
            // ============================================================================
            // Force balance: Top_force + Gravity_component - Bottom_force = Desired_tip_force
            // Sign convention: DOWN (toward tip) = positive, UP (away from tip) = negative
            //
            // Bottom cylinder: Counterbalances gravity when it pulls DOWN (pushes UP)
            // Top cylinder: Provides tip force, and counterbalances gravity when it pulls UP (pushes DOWN)
            
            float bottom_force_lbs = (gravity_component_lbs > 0.0f) ? gravity_component_lbs : 0.0f;
            float top_force_lbs = desired_tip_force_lbs - gravity_component_lbs + bottom_force_lbs;
            if (top_force_lbs < 0.0f) top_force_lbs = 0.0f;  // Cylinders can only push, not pull
            
            // ============================================================================
            // SECTION 5: Convert forces to air pressure (PSI)
            // ============================================================================
            // Pressure (PSI) = Force (lbs) / Area (sq in)
            // Area = π × (bore/2)² = π × bore² / 4
            float bottom_area_sqin = M_PI * bottom_cylinder_bore_inches * bottom_cylinder_bore_inches / 4.0f;
            float top_area_sqin = M_PI * top_cylinder_bore_inches * top_cylinder_bore_inches / 4.0f;
            
            float bottom_pressure_psi = (bottom_force_lbs > 0.0f) ? (bottom_force_lbs / bottom_area_sqin) : 0.0f;
            float top_pressure_psi = (top_force_lbs > 0.0f) ? (top_force_lbs / top_area_sqin) : 0.0f;
            
            // ============================================================================
            // SECTION 6: Format data for Input Assembly
            // ============================================================================
            // Convert to scaled integers: angles × 10000, pressures × 1000
            // Add bounds checking to prevent integer overflow
            // INT32_MAX = 2,147,483,647
            // For angles: max value = INT32_MAX / 10000 = 214,748.3647 degrees (way beyond physical limits)
            // For pressure: max value = INT32_MAX / 1000 = 2,147,483.647 PSI (way beyond physical limits)
            // Clamp values to reasonable physical limits to prevent overflow
            const float MAX_ANGLE_DEG = 180.0f;  // Physical limit: ±180 degrees
            const float MAX_PRESSURE_PSI = 10000.0f;  // Physical limit: 10,000 PSI (very high)
            
            float roll_clamped = fmaxf(-MAX_ANGLE_DEG, fminf(MAX_ANGLE_DEG, roll));
            float pitch_clamped = fmaxf(-MAX_ANGLE_DEG, fminf(MAX_ANGLE_DEG, pitch));
            float ground_angle_clamped = fmaxf(-MAX_ANGLE_DEG, fminf(MAX_ANGLE_DEG, signed_ground_angle));
            float bottom_pressure_clamped = fmaxf(0.0f, fminf(MAX_PRESSURE_PSI, bottom_pressure_psi));
            float top_pressure_clamped = fmaxf(0.0f, fminf(MAX_PRESSURE_PSI, top_pressure_psi));
            
            int32_t roll_scaled = (int32_t)roundf(roll_clamped * 10000.0f);
            int32_t pitch_scaled = (int32_t)roundf(pitch_clamped * 10000.0f);
            int32_t ground_angle_scaled = (int32_t)roundf(ground_angle_clamped * 10000.0f);
            int32_t bottom_pressure_scaled = (int32_t)roundf(bottom_pressure_clamped * 1000.0f);
            int32_t top_pressure_scaled = (int32_t)roundf(top_pressure_clamped * 1000.0f);
            
            // Format data into 20 bytes: 5 int32_t (roll, pitch, ground_angle, bottom_pressure, top_pressure) as little-endian
            uint8_t imu_data[20];
            imu_data[0] = (uint8_t)(roll_scaled & 0xFF);
            imu_data[1] = (uint8_t)((roll_scaled >> 8) & 0xFF);
            imu_data[2] = (uint8_t)((roll_scaled >> 16) & 0xFF);
            imu_data[3] = (uint8_t)((roll_scaled >> 24) & 0xFF);
            imu_data[4] = (uint8_t)(pitch_scaled & 0xFF);
            imu_data[5] = (uint8_t)((pitch_scaled >> 8) & 0xFF);
            imu_data[6] = (uint8_t)((pitch_scaled >> 16) & 0xFF);
            imu_data[7] = (uint8_t)((pitch_scaled >> 24) & 0xFF);
            imu_data[8] = (uint8_t)(ground_angle_scaled & 0xFF);
            imu_data[9] = (uint8_t)((ground_angle_scaled >> 8) & 0xFF);
            imu_data[10] = (uint8_t)((ground_angle_scaled >> 16) & 0xFF);
            imu_data[11] = (uint8_t)((ground_angle_scaled >> 24) & 0xFF);
            // Bottom cylinder pressure (PSI × 1000)
            imu_data[12] = (uint8_t)(bottom_pressure_scaled & 0xFF);
            imu_data[13] = (uint8_t)((bottom_pressure_scaled >> 8) & 0xFF);
            imu_data[14] = (uint8_t)((bottom_pressure_scaled >> 16) & 0xFF);
            imu_data[15] = (uint8_t)((bottom_pressure_scaled >> 24) & 0xFF);
            // Top cylinder pressure (PSI × 1000)
            imu_data[16] = (uint8_t)(top_pressure_scaled & 0xFF);
            imu_data[17] = (uint8_t)((top_pressure_scaled >> 8) & 0xFF);
            imu_data[18] = (uint8_t)((top_pressure_scaled >> 16) & 0xFF);
            imu_data[19] = (uint8_t)((top_pressure_scaled >> 24) & 0xFF);
            
            // Write to Input Assembly 100 with mutex protection
            if (assembly_mutex != NULL) {
                xSemaphoreTake(assembly_mutex, portMAX_DELAY);
            }
            
            // Check bounds again (now using 20 bytes total)
            const uint8_t imu_total_size = 20;  // 5 int32_t * 4 bytes each
            if (byte_start + imu_total_size <= sizeof(g_assembly_data064)) {
                memcpy(&g_assembly_data064[byte_start], imu_data, imu_total_size);
            } else {
                ESP_LOGW(TAG, "IMU: Byte range exceeds assembly size");
            }
            
            // Tool weight, tip force, and cylinder bore are read from Output Assembly 150 (bytes 29, 30, and 31)
            // These values are set by the EtherNet/IP client and used in calculations above
            // Byte 29: Cylinder bore (scaled by 100: 0 = use NVS, 1-255 = 0.01-2.55 inches)
            // Byte 30: Tool weight (0 = use NVS, 1-255 = weight in lbs)
            // Byte 31: Tip force (0 = use NVS, 1-255 = force in lbs)
            
            if (assembly_mutex != NULL) {
                xSemaphoreGive(assembly_mutex);
            }
        
        // Wait for next period
        vTaskDelayUntil(&last_wake_time, period_ms);
    }
}

