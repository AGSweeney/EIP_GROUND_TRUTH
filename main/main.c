#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
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

// MPU6050 device instance
static mpu6050_t s_mpu6050 = {0};
static i2c_master_dev_handle_t s_mpu6050_dev_handle = NULL;
static bool s_mpu6050_initialized = false;
static bool s_mpu6050_enabled_cached = false;  // Cache enabled state to avoid repeated NVS reads


// Sensor fusion state for absolute angle from vertical
static float s_fused_angle_from_vertical_rad = 0.0f;  // Fused angle estimate in radians
static int64_t s_last_fusion_time_us = 0;  // Last fusion update timestamp

static TaskHandle_t s_mpu6050_test_task_handle = NULL;
static TaskHandle_t s_mpu6050_io_task_handle = NULL;

// I2C bus handle (initialized in app_main)
static i2c_master_bus_handle_t s_i2c_bus_handle = NULL;

// Function to update MPU6050 enabled cache (called from API)
void sample_application_set_mpu6050_enabled(bool enabled)
{
    s_mpu6050_enabled_cached = enabled;
}

static void mpu6050_test_task(void *pvParameters);
static void mpu6050_io_task(void *pvParameters);
static void scan_i2c_bus(i2c_master_bus_handle_t bus_handle);
#if LWIP_IPV4 && LWIP_ACD
static struct acd s_static_ip_acd;
static bool s_acd_registered = false;
static SemaphoreHandle_t s_acd_sem = NULL;
static acd_callback_enum_t s_acd_last_state = ACD_RESTART_CLIENT;
static bool s_acd_probe_pending = false;
static esp_netif_ip_info_t s_pending_static_ip_cfg = {0};
#if LWIP_ACD_RFC5227_COMPLIANT_STATIC
static esp_netif_t *s_pending_esp_netif = NULL;  /* Store esp_netif for callback */
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

static void tcpip_try_pending_acd(esp_netif_t *netif, struct netif *lwip_netif);
static void tcpip_retry_acd_deferred(void *arg);

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

static void tcpip_acd_conflict_callback(struct netif *netif, acd_callback_enum_t state) {
    ESP_LOGI(TAG, "ACD callback state=%d", (int)state);
    s_acd_last_state = state;
    switch (state) {
        case ACD_IP_OK:
            g_tcpip.status &= ~(kTcpipStatusAcdStatus | kTcpipStatusAcdFault);
            CipTcpIpSetLastAcdActivity(0);
#if LWIP_ACD_RFC5227_COMPLIANT_STATIC
            /* With RFC 5227, IP is now assigned. Configure DNS and notify */
            if (netif != NULL && s_pending_esp_netif != NULL) {
                opener_configure_dns(s_pending_esp_netif);
                s_acd_probe_pending = false;
                ESP_LOGI(TAG, "RFC 5227: IP assigned after ACD confirmation");
            }
#endif
            break;
        case ACD_DECLINE:
        case ACD_RESTART_CLIENT:
            g_tcpip.status |= kTcpipStatusAcdStatus;
            g_tcpip.status |= kTcpipStatusAcdFault;
            CipTcpIpSetLastAcdActivity(3);
#if LWIP_ACD_RFC5227_COMPLIANT_STATIC
            /* With RFC 5227, IP was not assigned due to conflict */
            if (netif != NULL) {
                s_acd_probe_pending = false;
                s_pending_esp_netif = NULL;
                ESP_LOGW(TAG, "RFC 5227: IP not assigned due to conflict");
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
    AcdStartContext *ctx = (AcdStartContext *)arg;
    ctx->err = ERR_OK;
    if (!s_acd_registered) {
        ctx->netif->acd_list = NULL;
        memset(&s_static_ip_acd, 0, sizeof(s_static_ip_acd));
        if (acd_add(ctx->netif, &s_static_ip_acd, tcpip_acd_conflict_callback) == ERR_OK) {
            ESP_LOGI(TAG, "ACD client registered");
            s_acd_registered = true;
        } else {
            ctx->err = ERR_IF;
            return;
        }
    }
    acd_stop(&s_static_ip_acd);
    ESP_LOGI(TAG, "Starting ACD probe via acd_start()");
    ctx->err = acd_start(ctx->netif, &s_static_ip_acd, ctx->ip);
    ESP_LOGI(TAG, "acd_start() returned err=%d", (int)ctx->err);
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

    s_acd_last_state = ACD_RESTART_CLIENT;
    CipTcpIpSetLastAcdActivity(2);

    AcdStartContext ctx = {
        .netif = netif,
        .ip = *ip,
        .err = ERR_OK,
    };

    if (tcpip_callback_with_block(tcpip_acd_start_cb, &ctx, 1) != ERR_OK || ctx.err != ERR_OK) {
        ESP_LOGE(TAG, "Failed to start ACD probe (err=%d)", (int)ctx.err);
        g_tcpip.status |= kTcpipStatusAcdStatus | kTcpipStatusAcdFault;
        CipTcpIpSetLastAcdActivity(3);
        return false;
    }

    TickType_t wait_ticks = pdMS_TO_TICKS(500);

    if (xSemaphoreTake(s_acd_sem, wait_ticks) == pdTRUE) {
        ESP_LOGI(TAG, "ACD completed with state=%d", (int)s_acd_last_state);
        if (s_acd_last_state == ACD_IP_OK) {
            CipTcpIpSetLastAcdActivity(0);
            return true;
        }
    } else if (s_acd_last_state == ACD_IP_OK) {
        ESP_LOGW(TAG, "ACD completion detected without semaphore wake; continuing");
        CipTcpIpSetLastAcdActivity(0);
        return true;
    }

    if (s_acd_last_state == ACD_RESTART_CLIENT || s_acd_last_state == ACD_DECLINE) {
        ESP_LOGI(TAG, "ACD probe timed out without conflict response; continuing with static IP");
        CipTcpIpSetLastAcdActivity(0);
        return true;
    }

    ESP_LOGE(TAG, "ACD reported conflict (state=%d)", (int)s_acd_last_state);
    tcpip_callback_with_block(tcpip_acd_stop_cb, NULL, 1);
    CipTcpIpSetLastAcdActivity(3);
    return false;
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
    ctx->err = netif_set_addr_with_acd(ctx->netif, &ctx->ip, &ctx->netmask, &ctx->gw, 
                                        tcpip_acd_conflict_callback);
}
#endif

static void tcpip_try_pending_acd(esp_netif_t *netif, struct netif *lwip_netif) {
    if (!s_acd_probe_pending || netif == NULL || lwip_netif == NULL) {
        return;
    }
    if (!netif_has_valid_hwaddr(lwip_netif)) {
        ESP_LOGI(TAG, "ACD deferred until MAC address is available");
        return;
    }
    if (!netif_is_link_up(lwip_netif)) {
        ESP_LOGI(TAG, "ACD deferred until link is up");
        return;
    }

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
        ESP_LOGI(TAG, "RFC 5227: ACD started, IP assignment deferred");
        /* IP will be assigned when ACD_IP_OK callback is received */
        /* DNS will be configured in the callback */
    }
#else
    /* Legacy ACD flow: set IP first, then perform ACD */
    ip4_addr_t desired_ip = { .addr = s_pending_static_ip_cfg.ip.addr };
    CipTcpIpSetLastAcdActivity(2);
    if (!tcpip_perform_acd(lwip_netif, &desired_ip)) {
        ESP_LOGE(TAG, "Deferred ACD conflict detected for " IPSTR, IP2STR(&s_pending_static_ip_cfg.ip));
        ESP_LOGW(TAG, "Static configuration remains active despite ACD fault");
    }

    ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &s_pending_static_ip_cfg));
    opener_configure_dns(netif);
    s_acd_probe_pending = false;
    CipTcpIpSetLastAcdActivity(0);
#endif
}

static void tcpip_retry_acd_deferred(void *arg) {
    esp_netif_t *netif = (esp_netif_t *)arg;
    if (netif == NULL) {
        return;
    }
    struct netif *lwip_netif = (struct netif *)esp_netif_get_netif_impl(netif);
    tcpip_try_pending_acd(netif, lwip_netif);
}
#else
static bool tcpip_perform_acd(struct netif *netif, const ip4_addr_t *ip) {
    (void)netif;
    (void)ip;
    if (g_tcpip.select_acd) {
        ESP_LOGW(TAG, "ACD requested but not supported by lwIP configuration");
    }
    g_tcpip.status &= ~(kTcpipStatusAcdStatus | kTcpipStatusAcdFault);
    return true;
}
#endif

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
            ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip_info));
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
        
        // Start MPU6050 test task (always try to initialize if I2C bus is available)
        if (s_mpu6050_test_task_handle == NULL) {
            if (s_i2c_bus_handle != NULL) {
                BaseType_t result = xTaskCreatePinnedToCore(mpu6050_test_task,
                                                           "MPU6050_TEST",
                                                           4096,  // Stack size
                                                           NULL,
                                                           4,     // Priority (lower than MCP I/O)
                                                           &s_mpu6050_test_task_handle,
                                                           1);    // Core 1
                if (result == pdPASS) {
                    ESP_LOGI(TAG, "MPU6050 test task started on Core 1");
                } else {
                    ESP_LOGW(TAG, "Failed to create MPU6050 test task");
                }
            } else {
                ESP_LOGI(TAG, "MPU6050 test task skipped - I2C bus not available");
            }
        }
    } else {
        ESP_LOGE(TAG, "Failed to find netif");
    }
}

void app_main(void)
{
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

// Task to test MPU6050 sensor - initializes, verifies connection, and logs readings
static void mpu6050_test_task(void *pvParameters)
{
    (void)pvParameters;
    
    ESP_LOGI(TAG, "MPU6050 test task started");
    
    // Wait for I2C bus to be ready
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Wait for I2C bus to be initialized
    int retry_count = 0;
    while (s_i2c_bus_handle == NULL && retry_count < 10) {
        ESP_LOGW(TAG, "MPU6050: I2C bus handle not available, retrying... (%d/10)", retry_count + 1);
        vTaskDelay(pdMS_TO_TICKS(500));
        retry_count++;
    }
    
    if (s_i2c_bus_handle == NULL) {
        ESP_LOGE(TAG, "MPU6050: I2C bus handle is NULL after retries, cannot initialize");
        vTaskDelete(NULL);
        return;
    }
    
    i2c_master_bus_handle_t bus_handle = s_i2c_bus_handle;
    
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
        ESP_LOGW(TAG, "MPU6050: Device not detected at either address (0x%02X or 0x%02X)", 
                 MPU6050_I2C_ADDR_PRIMARY, MPU6050_I2C_ADDR_SECONDARY);
        ESP_LOGI(TAG, "MPU6050: Task will exit - device not present or I2C bus issue");
        vTaskDelete(NULL);
        return;
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
        vTaskDelete(NULL);
        return;
    }
    
    vTaskDelay(pdMS_TO_TICKS(200));  // Give I2C bus a moment to stabilize after adding device
    
    // Initialize MPU6050
    if (!mpu6050_init(&s_mpu6050, s_mpu6050_dev_handle)) {
        ESP_LOGE(TAG, "MPU6050: Failed to initialize device structure");
        i2c_master_bus_rm_device(s_mpu6050_dev_handle);
        vTaskDelete(NULL);
        return;
    }
    
    // Read WHO_AM_I register to verify connection with retries
    uint8_t who_am_i = 0;
    bool who_am_i_success = false;
    esp_err_t last_err = ESP_OK;
    
    for (int retry = 0; retry < 10; retry++) {  // Increased retries to 10
        err = mpu6050_read_who_am_i(&s_mpu6050, &who_am_i);
        last_err = err;
        
        if (err == ESP_OK) {
            who_am_i_success = true;
            break;
        }
        
        // Retry on timeout, invalid state, or any I2C error
        if (err == ESP_ERR_TIMEOUT || err == ESP_ERR_INVALID_STATE || 
            err == ESP_ERR_INVALID_RESPONSE || err == ESP_FAIL) {
            // Exponential backoff: 200ms, 400ms, 600ms, etc.
            int delay_ms = 200 * (retry + 1);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        } else {
            // Unexpected error, don't retry
            ESP_LOGE(TAG, "MPU6050: Unexpected error type, not retrying: %s", esp_err_to_name(err));
            break;
        }
    }
    
    if (!who_am_i_success) {
        ESP_LOGE(TAG, "MPU6050: Failed to read WHO_AM_I after %d attempts. Last error: %s", 
                 10, esp_err_to_name(last_err));
        ESP_LOGI(TAG, "MPU6050: This may indicate I2C bus contention or device not responding");
        i2c_master_bus_rm_device(s_mpu6050_dev_handle);
        vTaskDelete(NULL);
        return;
    }
    
    if (who_am_i == MPU6050_WHO_AM_I_VALUE) {
        // Standard MPU6050 chip (0x68)
        ESP_LOGI(TAG, "MPU6050: Standard chip detected (WHO_AM_I: 0x%02X)", who_am_i);
    } else if (who_am_i == 0x98) {
        // Chinese clone chip (0x98)
        ESP_LOGI(TAG, "MPU6050: Clone chip detected (WHO_AM_I: 0x%02X) - continuing with initialization", who_am_i);
    } else {
        // Unknown/unexpected value
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
    
    // Load enabled state and start I/O task (task will check enabled state)
    s_mpu6050_enabled_cached = system_mpu6050_enabled_load();
    
    // Always start I/O task if device is initialized (task checks enabled state)
    if (s_mpu6050_io_task_handle == NULL) {
        BaseType_t result = xTaskCreatePinnedToCore(mpu6050_io_task,
                                                   "MPU6050_IO",
                                                   4096,  // Stack size
                                                   NULL,
                                                   5,     // Priority (same as MCP I/O)
                                                   &s_mpu6050_io_task_handle,
                                                   1);    // Core 1
        if (result == pdPASS) {
        } else {
            ESP_LOGW(TAG, "Failed to create MPU6050 I/O task");
        }
    }
    
    // Task complete - initialization done
    vTaskDelete(NULL);
}


// Task to continuously read MPU6050 sensor data and write to Input Assembly 100
static void mpu6050_io_task(void *pvParameters)
{
    (void)pvParameters;
    
    // Wait for initialization to complete
    while (!s_mpu6050_initialized) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Get assembly mutex
    SemaphoreHandle_t assembly_mutex = sample_application_get_assembly_mutex();
    
    // Get byte offset from NVS
    uint8_t byte_start = system_mpu6050_byte_start_load();
    
    // Validate byte offset (MPU6050 uses 20 bytes: 5 int32_t for roll, pitch, ground_angle, bottom_pressure, top_pressure)
    // Values are stored as scaled integers: degrees * 10000, pressure * 1000
    const uint8_t mpu6050_data_size = 20;  // 5 int32_t * 4 bytes each
    if (byte_start + mpu6050_data_size > sizeof(g_assembly_data064)) {
        ESP_LOGE(TAG, "MPU6050: Invalid byte offset %d (would exceed assembly size for %d bytes)", byte_start, mpu6050_data_size);
        vTaskDelete(NULL);
        return;
    }
    
    // MPU6050 I/O task started - writing data to Input Assembly 100
    
    mpu6050_sample_t sample = {0};
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period_ms = pdMS_TO_TICKS(20);  // 50 Hz update rate (faster updates)
    
    while (1) {
        // Use cached enabled state (loaded at task startup and updated via API)
        // No need to poll NVS continuously - state changes are handled via API which updates the cache
        
        // If disabled, zero out assembly bytes and skip reading
        if (!s_mpu6050_enabled_cached) {
            if (assembly_mutex != NULL) {
                xSemaphoreTake(assembly_mutex, portMAX_DELAY);
            }
            memset(&g_assembly_data064[byte_start], 0, mpu6050_data_size);  // Clear all 20 bytes
            if (assembly_mutex != NULL) {
                xSemaphoreGive(assembly_mutex);
            }
            vTaskDelayUntil(&last_wake_time, period_ms);
            continue;
        }
        
        // Read accelerometer and gyroscope data (single read for speed, sensor fusion handles noise)
        // For even faster updates, we can reduce averaging - sensor fusion provides stability
        mpu6050_accel_t accel;
        mpu6050_gyro_t gyro;
        esp_err_t accel_err = mpu6050_read_accel(&s_mpu6050, &accel);
        esp_err_t gyro_err = mpu6050_read_gyro(&s_mpu6050, &gyro);
        
        if (accel_err != ESP_OK || gyro_err != ESP_OK) {
            ESP_LOGW(TAG, "MPU6050: Failed to read sensor data");
            vTaskDelayUntil(&last_wake_time, period_ms);
            continue;
        }
        
        
        sample.accel = accel;
        sample.gyro = gyro;
        
        // Debug: Log corrected accelerometer values periodically (every 5 seconds)
        // Calculate orientation (roll, pitch, ground_angle) from corrected accelerometer reading
        mpu6050_orientation_t orientation;
        esp_err_t err = mpu6050_calculate_orientation(&s_mpu6050, &sample.accel, &orientation);
        
        if (err == ESP_OK) {
            // ============================================================================
            // SECTION 1: Calculate signed ground angle for display
            // ============================================================================
            // Sign indicates force reversal: positive (0-90°), negative (90-180°)
            float signed_ground_angle;
            if (orientation.abs_ground_angle > 0.0f) {
                signed_ground_angle = (orientation.abs_ground_angle > 90.0f) 
                    ? -orientation.abs_ground_angle  // Past 90°: force reverses
                    : orientation.abs_ground_angle;  // 0-90°: normal direction
            } else {
                signed_ground_angle = 0.0f;
            }
            
            // ============================================================================
            // SECTION 2: Load configuration parameters
            // ============================================================================
            // Read tool weight and tip force from Output Assembly 150 (bytes 30, 31)
            // Fall back to NVS if assembly bytes are 0
            float tool_weight_lbs = 50.0f;  // Default
            float desired_tip_force_lbs = 20.0f;  // Default
            
            if (assembly_mutex != NULL) {
                xSemaphoreTake(assembly_mutex, portMAX_DELAY);
            }
            
            if (sizeof(g_assembly_data096) >= 32) {
                uint8_t tool_weight_byte = g_assembly_data096[30];
                tool_weight_lbs = (tool_weight_byte > 0) 
                    ? (float)tool_weight_byte 
                    : (float)system_tool_weight_load();
                
                uint8_t tip_force_byte = g_assembly_data096[31];
                desired_tip_force_lbs = (tip_force_byte > 0) 
                    ? (float)tip_force_byte 
                    : (float)system_tip_force_load();
            }
            
            if (assembly_mutex != NULL) {
                xSemaphoreGive(assembly_mutex);
            }
            
            // Load cylinder bore size from NVS (defaults to 1.0 inch)
            const float cylinder_bore_inches = system_cylinder_bore_load();
            const float bottom_cylinder_bore_inches = cylinder_bore_inches;
            const float top_cylinder_bore_inches = cylinder_bore_inches;
            
            // ============================================================================
            // SECTION 3: Sensor fusion - calculate absolute angle from vertical
            // ============================================================================
            // Complementary filter combines:
            //   - Accelerometer: accurate long-term, noisy during motion
            //   - Gyroscope: accurate short-term, drifts over time
            
            // Convert accelerometer to g units
            float accel_x_g = (float)sample.accel.x * s_mpu6050.accel_scale;
            float accel_y_g = (float)sample.accel.y * s_mpu6050.accel_scale;
            float accel_z_g = (float)sample.accel.z * s_mpu6050.accel_scale;
            float accel_total_g = sqrtf(accel_x_g * accel_x_g + accel_y_g * accel_y_g + accel_z_g * accel_z_g);
            
            // Calculate angle from vertical using accelerometer (arccos of normalized Z)
            float angle_from_accel_rad = 0.0f;
            if (accel_total_g > 0.1f) {
                float cos_angle = accel_z_g / accel_total_g;
                if (cos_angle > 1.0f) cos_angle = 1.0f;
                if (cos_angle < -1.0f) cos_angle = -1.0f;
                angle_from_accel_rad = acosf(cos_angle);
            }
            
            // Calculate angular rate from gyroscope (magnitude of X and Y components)
            float gyro_x_dps = (float)sample.gyro.x * s_mpu6050.gyro_scale;
            float gyro_y_dps = (float)sample.gyro.y * s_mpu6050.gyro_scale;
            float gyro_rate_dps = sqrtf(gyro_x_dps * gyro_x_dps + gyro_y_dps * gyro_y_dps);
            float gyro_rate_rad_per_s = gyro_rate_dps * M_PI / 180.0f;
            
            // Calculate time delta for integration
            int64_t current_time_us = esp_timer_get_time();
            float dt = (s_last_fusion_time_us > 0) 
                ? (float)(current_time_us - s_last_fusion_time_us) / 1000000.0f 
                : 0.0f;
            
            // Initialize on first run
            if (s_last_fusion_time_us == 0) {
                s_fused_angle_from_vertical_rad = angle_from_accel_rad;
            }
            
            // Complementary filter: alpha = 0.96 (96% gyro, 4% accelerometer)
            const float alpha = 0.96f;
            if (dt > 0.0f && dt < 1.0f) {
                s_fused_angle_from_vertical_rad = alpha * (s_fused_angle_from_vertical_rad + gyro_rate_rad_per_s * dt) 
                                                   + (1.0f - alpha) * angle_from_accel_rad;
            } else {
                s_fused_angle_from_vertical_rad = angle_from_accel_rad;  // Invalid dt, use accel only
            }
            
            s_last_fusion_time_us = current_time_us;
            
            // Clamp to valid range [0, π] (0-180°)
            if (s_fused_angle_from_vertical_rad < 0.0f) s_fused_angle_from_vertical_rad = 0.0f;
            if (s_fused_angle_from_vertical_rad > M_PI) s_fused_angle_from_vertical_rad = M_PI;
            
            // Calculate gravity component along slide axis
            float cos_angle_from_vertical = cosf(s_fused_angle_from_vertical_rad);
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
            int32_t roll_scaled = (int32_t)roundf(orientation.roll * 10000.0f);
            int32_t pitch_scaled = (int32_t)roundf(orientation.pitch * 10000.0f);
            int32_t ground_angle_scaled = (int32_t)roundf(signed_ground_angle * 10000.0f);
            int32_t bottom_pressure_scaled = (int32_t)roundf(bottom_pressure_psi * 1000.0f);
            int32_t top_pressure_scaled = (int32_t)roundf(top_pressure_psi * 1000.0f);
            
            // Format data into 20 bytes: 5 int32_t (roll, pitch, ground_angle, bottom_pressure, top_pressure) as little-endian
            uint8_t mpu6050_data[20];
            mpu6050_data[0] = (uint8_t)(roll_scaled & 0xFF);
            mpu6050_data[1] = (uint8_t)((roll_scaled >> 8) & 0xFF);
            mpu6050_data[2] = (uint8_t)((roll_scaled >> 16) & 0xFF);
            mpu6050_data[3] = (uint8_t)((roll_scaled >> 24) & 0xFF);
            mpu6050_data[4] = (uint8_t)(pitch_scaled & 0xFF);
            mpu6050_data[5] = (uint8_t)((pitch_scaled >> 8) & 0xFF);
            mpu6050_data[6] = (uint8_t)((pitch_scaled >> 16) & 0xFF);
            mpu6050_data[7] = (uint8_t)((pitch_scaled >> 24) & 0xFF);
            mpu6050_data[8] = (uint8_t)(ground_angle_scaled & 0xFF);
            mpu6050_data[9] = (uint8_t)((ground_angle_scaled >> 8) & 0xFF);
            mpu6050_data[10] = (uint8_t)((ground_angle_scaled >> 16) & 0xFF);
            mpu6050_data[11] = (uint8_t)((ground_angle_scaled >> 24) & 0xFF);
            // Bottom cylinder pressure (PSI × 1000)
            mpu6050_data[12] = (uint8_t)(bottom_pressure_scaled & 0xFF);
            mpu6050_data[13] = (uint8_t)((bottom_pressure_scaled >> 8) & 0xFF);
            mpu6050_data[14] = (uint8_t)((bottom_pressure_scaled >> 16) & 0xFF);
            mpu6050_data[15] = (uint8_t)((bottom_pressure_scaled >> 24) & 0xFF);
            // Top cylinder pressure (PSI × 1000)
            mpu6050_data[16] = (uint8_t)(top_pressure_scaled & 0xFF);
            mpu6050_data[17] = (uint8_t)((top_pressure_scaled >> 8) & 0xFF);
            mpu6050_data[18] = (uint8_t)((top_pressure_scaled >> 16) & 0xFF);
            mpu6050_data[19] = (uint8_t)((top_pressure_scaled >> 24) & 0xFF);
            
            // Write to Input Assembly 100 with mutex protection
            if (assembly_mutex != NULL) {
                xSemaphoreTake(assembly_mutex, portMAX_DELAY);
            }
            
            // Check bounds again (now using 20 bytes total)
            const uint8_t mpu6050_total_size = 20;  // 5 int32_t * 4 bytes each
            if (byte_start + mpu6050_total_size <= sizeof(g_assembly_data064)) {
                memcpy(&g_assembly_data064[byte_start], mpu6050_data, mpu6050_total_size);
            } else {
                ESP_LOGW(TAG, "MPU6050: Byte range exceeds assembly size");
            }
            
            // Tool weight and tip force are read from Output Assembly 150 (bytes 30 and 31)
            // These values are set by the EtherNet/IP client and used in calculations above
            
            if (assembly_mutex != NULL) {
                xSemaphoreGive(assembly_mutex);
            }
        } else {
            // Log orientation calculation errors occasionally
            static uint32_t calc_error_count = 0;
            calc_error_count++;
            if (calc_error_count % 50 == 1) {
                ESP_LOGW(TAG, "MPU6050: Failed to calculate orientation: %s (error #%lu)", 
                        esp_err_to_name(err), calc_error_count);
            }
        }
        
        // Wait for next period
        vTaskDelayUntil(&last_wake_time, period_ms);
    }
}

