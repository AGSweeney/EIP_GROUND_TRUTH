# Integration Guide for 4-20mA Current Loop Component

## Quick Start

This component is **NOT enabled by default**. To enable it:

### Step 1: Enable Component Build

Edit `components/current_loop_4_20ma/CMakeLists.txt`:

```cmake
# Uncomment this section:
idf_component_register(
    SRCS 
        "current_loop_4_20ma.c"
    INCLUDE_DIRS 
        "include"
    REQUIRES 
        driver
)
```

### Step 2: Add Kconfig Option

Add to `main/Kconfig.projbuild`:

```kconfig
menu "OpenER 4-20mA Output Configuration"
    config OPENER_4_20MA_PWM_GPIO
        int "4-20mA PWM GPIO pin"
        range 0 54
        default 9
        help
            GPIO pin number for 4-20mA current loop PWM output
            This pin will output PWM signal (0-3.3V) to drive XTR111
endmenu
```

### Step 3: Include Header in Main Code

Add to `main/main.c`:

```c
#include "current_loop_4_20ma.h"
```

### Step 4: Initialize in app_main()

In `main/main.c`, in `got_ip_event_handler()` after network initialization:

```c
// Initialize 4-20mA output
if (!current_loop_4_20ma_init(CONFIG_OPENER_4_20MA_PWM_GPIO)) {
    ESP_LOGW(TAG, "Failed to initialize 4-20mA output");
} else {
    ESP_LOGI(TAG, "4-20mA output initialized on GPIO%d", CONFIG_OPENER_4_20MA_PWM_GPIO);
}
```

### Step 5: Update from EtherNet/IP Assembly

Create a task or update existing assembly task to read from Output Assembly 150:

```c
void current_loop_assembly_update_task(void *pvParameters)
{
    (void)pvParameters;
    
    SemaphoreHandle_t assembly_mutex = sample_application_get_assembly_mutex();
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period_ms = pdMS_TO_TICKS(50);  // 20Hz update rate
    
    while (1) {
        if (assembly_mutex != NULL) {
            xSemaphoreTake(assembly_mutex, portMAX_DELAY);
        }
        
        // Read from Output Assembly 150, byte 0
        // (adjust byte offset as needed)
        uint8_t current_byte = g_assembly_data096[0];
        
        if (assembly_mutex != NULL) {
            xSemaphoreGive(assembly_mutex);
        }
        
        // Update 4-20mA output
        current_loop_4_20ma_update_from_assembly(current_byte);
        
        vTaskDelayUntil(&last_wake_time, period_ms);
    }
}
```

Start the task after initialization:

```c
// Start 4-20mA update task
xTaskCreatePinnedToCore(current_loop_assembly_update_task,
                       "4_20ma_Update",
                       2048,  // Stack size
                       NULL,
                       3,     // Priority
                       NULL,
                       1);    // Core 1
```

## Assembly 150 Byte Mapping

Recommended mapping for Output Assembly 150:

| Byte Range | Usage | Description |
|------------|-------|-------------|
| 0 | 4-20mA Output | Primary 4-20mA current loop output (0-255 → 4-20mA) |
| 1-29 | Available | Reserved for other outputs or control data |
| 30 | Tool Weight | Tool weight (used by MPU6050) |
| 31 | Tip Force | Tip force (used by MPU6050) |

You can map the 4-20mA output to any byte 0-29. Adjust the byte offset in your update code accordingly.

## Testing

### Manual Testing

Test the 4-20mA output manually:

```c
// Set to 4mA (minimum)
current_loop_4_20ma_set(4.0f);

// Set to 12mA (mid-range)
current_loop_4_20ma_set(12.0f);

// Set to 20mA (maximum)
current_loop_4_20ma_set(20.0f);

// Get current setting
float current = current_loop_4_20ma_get();
ESP_LOGI(TAG, "Current output: %.2f mA", current);
```

### Via EtherNet/IP

1. Connect EtherNet/IP controller
2. Write value to Output Assembly 150, byte 0:
   - 0 → 4mA output
   - 128 → 12mA output
   - 255 → 20mA output
3. Measure current with multimeter in series with loop

## Hardware Verification

1. **Check PWM Output**: Use oscilloscope to verify PWM on configured GPIO
2. **Check Filter Output**: Measure DC voltage after low-pass filter (should be 0-3.3V)
3. **Check XTR111 Input**: Measure voltage at XTR111 VIN pin (should be 0-2.5V if divider used)
4. **Check Loop Current**: Measure current in loop with multimeter (should be 4-20mA)

## Troubleshooting

See the README.md in this directory for detailed troubleshooting information.

