/**
 * @file current_loop_4_20ma.c
 * @brief 4-20mA Current Loop Output Driver Implementation
 * 
 * @note This component is currently NOT included in the build.
 *       To enable, uncomment the component registration in CMakeLists.txt
 */

#include "current_loop_4_20ma.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "4_20ma";

// LEDC configuration for PWM output
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // 8192 steps (13-bit resolution)
#define LEDC_FREQUENCY          1000              // 1kHz PWM frequency

// Current limits
#define CURRENT_MIN_MA          4.0f
#define CURRENT_MAX_MA          20.0f
#define CURRENT_RANGE_MA        16.0f  // 20.0 - 4.0

// Voltage ranges
#define VOLTAGE_MAX_V           2.5f   // Maximum input to XTR111
#define ESP32_VOLTAGE_MAX_V     3.3f   // ESP32 GPIO voltage

// Static state
static int s_pwm_gpio = -1;
static float s_current_ma = CURRENT_MIN_MA;

bool current_loop_4_20ma_init(int pwm_gpio)
{
    if (pwm_gpio < 0 || pwm_gpio > 54) {
        ESP_LOGE(TAG, "Invalid GPIO pin: %d", pwm_gpio);
        return false;
    }
    
    s_pwm_gpio = pwm_gpio;
    
    // Configure LEDC timer
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_DUTY_RES,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    esp_err_t err = ledc_timer_config(&ledc_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC timer: %s", esp_err_to_name(err));
        return false;
    }
    
    // Configure LEDC channel
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = pwm_gpio,
        .duty           = 0,
        .hpoint         = 0
    };
    err = ledc_channel_config(&ledc_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC channel: %s", esp_err_to_name(err));
        return false;
    }
    
    // Set initial output to 4mA (minimum)
    current_loop_4_20ma_set(CURRENT_MIN_MA);
    
    ESP_LOGI(TAG, "4-20mA current loop initialized on GPIO%d", pwm_gpio);
    return true;
}

bool current_loop_4_20ma_set(float current_ma)
{
    if (s_pwm_gpio < 0) {
        ESP_LOGE(TAG, "4-20mA not initialized");
        return false;
    }
    
    // Clamp to valid range
    if (current_ma < CURRENT_MIN_MA) {
        ESP_LOGW(TAG, "Current %.2f mA below minimum, clamping to %.2f mA", 
                 current_ma, CURRENT_MIN_MA);
        current_ma = CURRENT_MIN_MA;
    } else if (current_ma > CURRENT_MAX_MA) {
        ESP_LOGW(TAG, "Current %.2f mA above maximum, clamping to %.2f mA", 
                 current_ma, CURRENT_MAX_MA);
        current_ma = CURRENT_MAX_MA;
    }
    
    s_current_ma = current_ma;
    
    // Convert 4-20mA to voltage (0-2.5V for XTR111)
    // Formula: V = (I - 4mA) / 16mA × 2.5V
    float voltage = ((current_ma - CURRENT_MIN_MA) / CURRENT_RANGE_MA) * VOLTAGE_MAX_V;
    
    // Convert voltage to PWM duty cycle
    // ESP32-P4 PWM: 0-3.3V range, 13-bit resolution (0-8191)
    // Note: If using voltage divider, adjust this calculation accordingly
    uint32_t duty = (uint32_t)((voltage / ESP32_VOLTAGE_MAX_V) * ((1 << LEDC_DUTY_RES) - 1));
    
    // Clamp duty to valid range
    uint32_t max_duty = (1 << LEDC_DUTY_RES) - 1;
    if (duty > max_duty) {
        duty = max_duty;
    }
    
    // Set PWM duty
    esp_err_t err = ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set PWM duty: %s", esp_err_to_name(err));
        return false;
    }
    
    err = ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update PWM duty: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGD(TAG, "Set 4-20mA output: %.2f mA (voltage: %.3fV, duty: %lu/%lu)", 
             current_ma, voltage, duty, max_duty);
    
    return true;
}

float current_loop_4_20ma_get(void)
{
    return s_current_ma;
}

void current_loop_4_20ma_update_from_assembly(uint8_t assembly_byte)
{
    // Map byte value (0-255) to current (4-20mA)
    // Linear mapping: 0 -> 4mA, 255 -> 20mA
    // Formula: Current (mA) = 4.0 + (byte / 255.0) × 16.0
    float current_ma = CURRENT_MIN_MA + ((float)assembly_byte / 255.0f) * CURRENT_RANGE_MA;
    current_loop_4_20ma_set(current_ma);
}

