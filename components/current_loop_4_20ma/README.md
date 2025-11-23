# 4-20mA Current Loop Output Component

A component for driving 4-20mA industrial current loops from ESP32-P4 using PWM output and XTR111 current loop transmitter IC.

## Overview

This component provides an interface to control 4-20mA current loop outputs, commonly used in industrial automation. It uses PWM output from ESP32-P4 GPIO pins to drive an external XTR111 current loop transmitter IC.

## Features

- **PWM-based output**: Uses ESP32 LEDC (PWM) to generate 0-3.3V analog signal
- **Linear current mapping**: Direct conversion from assembly data bytes to current values
- **Range validation**: Automatically clamps values to 4-20mA range
- **EtherNet/IP integration**: Designed to work with EtherNet/IP Output Assembly 150
- **Configurable GPIO**: GPIO pin configurable via Kconfig

## Hardware Requirements

### Required Components

- **XTR111**: Texas Instruments 4-20mA current loop transmitter (SOIC-8 package)
- **Resistors**:
  - 10kΩ ±1% (PWM filter and XTR111 RLIM)
  - 33kΩ ±1% (optional voltage divider)
  - 100kΩ ±1% (optional voltage divider)
- **Capacitors**:
  - 1µF tantalum (PWM filter and VIN filtering)
  - 0.1µF ceramic (XTR111 decoupling)
- **Power Supply**: 24V DC (separate from ESP32 power) for current loop

### Circuit Connection

```
ESP32-P4 GPIO (PWM) → Low-pass filter → Voltage divider (optional) → XTR111 VIN
XTR111 IOUT → 4-20mA current loop → Industrial device
XTR111 V+ → 24V DC power supply
```

See the circuit diagram in the main project README for detailed schematic.

## Software Configuration

### Kconfig Options

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

### PWM Configuration

- **Frequency**: 1kHz (configurable in code)
- **Resolution**: 13-bit (8192 steps)
- **Output Range**: 0-3.3V (filtered to DC)
- **XTR111 Input**: 0-2.5V (via optional voltage divider)

## API Reference

### Initialization

```c
bool current_loop_4_20ma_init(int pwm_gpio);
```

Initialize 4-20mA current loop output on specified GPIO pin.

**Parameters:**
- `pwm_gpio`: GPIO pin number (0-54)

**Returns:**
- `true`: Initialization successful
- `false`: Initialization failed

**Example:**
```c
if (current_loop_4_20ma_init(CONFIG_OPENER_4_20MA_PWM_GPIO)) {
    ESP_LOGI(TAG, "4-20mA output initialized");
}
```

### Set Current Output

```c
bool current_loop_4_20ma_set(float current_ma);
```

Set the 4-20mA output to specified current value.

**Parameters:**
- `current_ma`: Desired current in milliamps (4.0 to 20.0)

**Returns:**
- `true`: Current set successfully
- `false`: Invalid value or error

**Example:**
```c
// Set output to 12mA
current_loop_4_20ma_set(12.0f);
```

### Get Current Setting

```c
float current_loop_4_20ma_get(void);
```

Get the current 4-20mA output setting.

**Returns:**
- Current setting in milliamps (float)

**Example:**
```c
float current = current_loop_4_20ma_get();
ESP_LOGI(TAG, "Current output: %.2f mA", current);
```

### Update from EtherNet/IP Assembly

```c
void current_loop_4_20ma_update_from_assembly(uint8_t assembly_byte);
```

Update 4-20mA output from EtherNet/IP Output Assembly byte value.

**Parameters:**
- `assembly_byte`: Byte value from Output Assembly (0-255)
  - 0 → 4mA
  - 255 → 20mA
  - Linear mapping for values in between

**Example:**
```c
// Read from Output Assembly 150, byte 0
uint8_t current_byte = g_assembly_data096[0];
current_loop_4_20ma_update_from_assembly(current_byte);
```

## Current Mapping

The component provides linear mapping from byte values to current:

| Assembly Byte | Current Output | Voltage to XTR111 |
|---------------|----------------|-------------------|
| 0             | 4.0 mA         | 0.0 V             |
| 64            | 8.0 mA         | 0.625 V           |
| 128           | 12.0 mA        | 1.25 V            |
| 192           | 16.0 mA        | 1.875 V           |
| 255           | 20.0 mA        | 2.5 V             |

**Formula:**
```
Current (mA) = 4.0 + (byte / 255.0) × 16.0
Voltage (V) = (Current - 4.0) / 16.0 × 2.5
```

## Integration with EtherNet/IP

### Output Assembly 150 Mapping

The 4-20mA output can be mapped to any byte in Output Assembly 150 (bytes 0-29 available, bytes 30-31 reserved for tool weight/tip force).

**Recommended Mapping:**
- **Byte 0**: Primary 4-20mA output (0-255 → 4-20mA)
- Additional outputs can use bytes 1-29 if multiple current loops are needed

### Example Integration

```c
// In your assembly update task or EtherNet/IP callback
void update_current_loop_from_assembly(void)
{
    SemaphoreHandle_t assembly_mutex = sample_application_get_assembly_mutex();
    if (assembly_mutex != NULL) {
        xSemaphoreTake(assembly_mutex, portMAX_DELAY);
        
        // Read from Output Assembly 150, byte 0
        uint8_t current_byte = g_assembly_data096[0];
        
        xSemaphoreGive(assembly_mutex);
        
        // Update 4-20mA output
        current_loop_4_20ma_update_from_assembly(current_byte);
    }
}
```

## Power Supply Requirements

### Separate 24V Supply

The XTR111 requires a separate 24V DC power supply for the current loop:

- **Voltage**: 24V DC (±10%)
- **Current**: Must supply 4-20mA plus loop load
- **Isolation**: Recommended to isolate from ESP32 3.3V/5V supply for safety
- **Protection**: Add reverse polarity protection if needed

### Loop Load Considerations

Maximum loop resistance depends on supply voltage:
- **24V supply**: Maximum ~750Ω loop resistance (including device)
- **12V supply**: Maximum ~250Ω loop resistance (including device)

**Formula:**
```
Max Resistance = (Supply Voltage - 4V) / 0.020A
```

## Troubleshooting

### No Current Output

1. Check GPIO pin configuration
2. Verify 24V power supply is connected to XTR111 V+
3. Check PWM filter circuit (R1, C1)
4. Verify XTR111 connections
5. Check loop load resistance is within limits

### Current Not Accurate

1. Verify PWM frequency and duty cycle calculations
2. Check voltage divider resistors (if used)
3. Calibrate XTR111 RLIM resistor (10kΩ nominal)
4. Verify loop load resistance

### Current Drift

1. Check power supply stability (24V)
2. Verify capacitor values in filter circuit
3. Check for temperature effects on components

## Safety Notes

⚠️ **Important Safety Considerations:**

1. **Isolation**: Use isolated 24V power supply in industrial environments
2. **Protection**: Add overcurrent protection (fuse) in series with 24V supply
3. **Reverse Polarity**: Protect against reverse connection of 24V supply
4. **Grounding**: Ensure proper grounding and isolation between ESP32 and loop
5. **Loop Wiring**: Use shielded cable for long loop runs in noisy environments

## References

- [XTR111 Datasheet](https://www.ti.com/lit/ds/symlink/xtr111.pdf)
- [ESP32 LEDC (PWM) Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/ledc.html)
- [4-20mA Current Loop Basics](https://en.wikipedia.org/wiki/Current_loop)

## Example Usage

See the main project `main/main.c` for complete integration example after enabling this component in the build.

---

**Status**: Not included in build by default  
**Version**: 1.0  
**Last Updated**: 2024

