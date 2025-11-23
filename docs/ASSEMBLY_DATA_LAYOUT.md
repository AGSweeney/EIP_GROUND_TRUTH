# EtherNet/IP Assembly Data Layout

This document describes the exact byte-by-byte layout of data in the EtherNet/IP assemblies.

## Overview

The device exposes three EtherNet/IP assembly instances:

- **Assembly 100 (Input)**: 32 bytes - Input data from sensors and I/O
- **Assembly 150 (Output)**: 32 bytes - Output data to actuators and control
- **Assembly 151 (Configuration)**: 10 bytes - Configuration parameters

All data is stored in **little-endian** byte order.

---

## Assembly 100 (Input Assembly) - 32 Bytes

The Input Assembly contains sensor data and I/O input states. Data sources can be configured with byte offsets to avoid conflicts.

### Default Layout (Typical Configuration)

| Byte Range | Size | Data Source | Description | Format |
|------------|------|-------------|-------------|--------|
| 0-15 | 16 bytes | MPU6050 IMU | Fused angle, cylinder pressures, temperature | 4 × int32_t (little-endian) |
| 16-31 | 16 bytes | Available | Reserved for other sensors | - |

### MPU6050 IMU Data

The MPU6050 sensor writes 16 bytes of data starting at byte offset 0 (fixed position).

**MPU6050 Data Structure (16 bytes total):**

| Byte Offset | Size | Field Name | Description | Format |
|-------------|------|------------|-------------|--------|
| 0-3 | 4 bytes | Fused Angle | Fused angle from vertical (sensor fusion) | int32_t (degrees × 100) |
| 4-7 | 4 bytes | Cylinder 1 Pressure | Bottom cylinder pressure | int32_t (PSI × 10) |
| 8-11 | 4 bytes | Cylinder 2 Pressure | Top cylinder pressure | int32_t (PSI × 10) |
| 12-15 | 4 bytes | Temperature | MPU6050 internal temperature | int32_t (Celsius × 100) |

**Example Values:**
- Fused Angle: 12.34° = 1234 (0x000004D2)
- Cylinder 1 Pressure: 123.4 PSI = 1234 (0x000004D2)
- Cylinder 2 Pressure: 87.6 PSI = 876 (0x0000036C)
- Temperature: 25.50°C = 2550 (0x000009F6)

**Note:** 
- The MPU6050 uses sensor fusion (complementary filter) combining accelerometer and gyroscope data
- Cylinder pressures are calculated based on the fused angle and tool weight/tip force from Output Assembly 150
- Data is written continuously at 10Hz (100ms update rate)

### Reserved Space (Bytes 20-31)

Bytes 20-31 are available for other sensors or I/O data. Currently unused.

### Example Layout

```
Byte:  0    1    2    3    4    5    6    7    8    9    10   11   12   13   14   15
      ┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┐
      │ Fused Angle (bytes 0-3)        │ Cylinder 1 Pressure (bytes 4-7)                │
      └────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┘

Byte:  16   17   18   19   20   21   22   23   24   25   26   27   28   29   30   31
      ┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┐
      │ Cylinder 2 Pressure (bytes 8-11)│ Temperature (bytes 12-15)│ Reserved (16-31)   │
      └────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┘
```

---

## Assembly 150 (Output Assembly) - 32 Bytes

The Output Assembly contains control data and output states sent from the EtherNet/IP controller.

### Fixed Layout

| Byte Range | Size | Field Name | Description | Format |
|------------|------|------------|-------------|--------|
| 0-29 | 30 bytes | Control Data | General control and output data | Configurable |
| 30 | 1 byte | Tool Weight | Tool weight in pounds | uint8_t (0-255 lbs) |
| 31 | 1 byte | Tip Force | Desired tip force in pounds | uint8_t (0-255 lbs) |

### Tool Weight and Tip Force (Bytes 30-31)

These values are used by the MPU6050 sensor fusion algorithm to calculate cylinder pressures:

- **Tool Weight (Byte 30)**: Weight of the tool in pounds (0-255 lbs)
  - Default: 50 lbs (if byte is 0, falls back to NVS stored value)
  - Used to calculate gravity component: `gravity = tool_weight × cos(angle_from_vertical)`

- **Tip Force (Byte 31)**: Desired tip force in pounds (0-255 lbs)
  - Default: 20 lbs (if byte is 0, falls back to NVS stored value)
  - Used in force balance equation: `top_force = tip_force - gravity_component + bottom_force`

**Note:** If either byte is 0, the system falls back to values stored in NVS (Non-Volatile Storage).

### Example Layout

```
Byte:  0    1    2    3    4    5    6    7    8    9    10   11   12   13   14   15
      ┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┐
      │ Control Data (bytes 0-29) - Available for application use                      │
      └────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┘

Byte:  16   17   18   19   20   21   22   23   24   25   26   27   28   29   30   31
      ┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┐
      │ Control Data (continued)                    │ Tool │ Tip │
      │                                               │Weight│Force│
      └────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┘
```

---

## Assembly 151 (Configuration Assembly) - 10 Bytes

The Configuration Assembly contains device configuration parameters.

### Layout

| Byte Range | Size | Field Name | Description | Format |
|------------|------|------------|-------------|--------|
| 0-9 | 10 bytes | Configuration | Configuration parameters | TBD |

**Note:** Configuration assembly structure is currently reserved for future use.

---

## Data Format Details

### Integer Formats

All multi-byte integers are stored in **little-endian** format (least significant byte first):

**int32_t (4 bytes):**
```
Byte 0: LSB (bits 0-7)
Byte 1: (bits 8-15)
Byte 2: (bits 16-23)
Byte 3: MSB (bits 24-31, sign bit)
```

**Example:** Value 123400 (0x0001E208) stored as:
- Byte 0: 0x08
- Byte 1: 0xE2
- Byte 2: 0x01
- Byte 3: 0x00

**uint16_t (2 bytes):**
```
Byte 0: LSB (bits 0-7)
Byte 1: MSB (bits 8-15)
```

**Example:** Value 0x1234 stored as:
- Byte 0: 0x34
- Byte 1: 0x12

### Scaled Integer Values

**MPU6050 Fused Angle:**
- Format: `int32_t` (degrees × 100)
- Range: -2147483.48° to +2147483.47°
- Example: 12.34° = 1234
- Example: -5.67° = -567

**MPU6050 Cylinder Pressures:**
- Format: `int32_t` (PSI × 10)
- Range: -2147483.6 PSI to +2147483.6 PSI
- Example: 123.4 PSI = 1234
- Example: 45.2 PSI = 452

**MPU6050 Temperature:**
- Format: `int32_t` (Celsius × 100)
- Range: -214748.36°C to +214748.36°C
- Example: 25.50°C = 2550
- Example: -10.25°C = -1025

**Tool Weight / Tip Force:**
- Format: `uint8_t` (pounds)
- Range: 0-255 lbs
- Example: 50 lbs = 50 (0x32)

---

## Modbus TCP Mapping

The assemblies are also accessible via Modbus TCP (port 502):

### Input Assembly 100 → Modbus Input Registers

- **Modbus Function**: Read Input Registers (0x04)
- **Register Range**: 0-15 (16 registers = 32 bytes)
- **Mapping**: Direct byte-to-register mapping
- **Endianness**: Assembly is little-endian, Modbus converts to big-endian

| Assembly Byte | Modbus Register | Notes |
|---------------|-----------------|-------|
| 0-1 | 0 | Little-endian in assembly, big-endian in Modbus |
| 2-3 | 1 | |
| ... | ... | |
| 30-31 | 15 | |

### Output Assembly 150 → Modbus Holding Registers

- **Modbus Function**: Read/Write Holding Registers (0x03, 0x06, 0x10)
- **Register Range**: 100-115 (16 registers = 32 bytes)
- **Mapping**: Direct byte-to-register mapping
- **Endianness**: Assembly is little-endian, Modbus converts to big-endian

| Assembly Byte | Modbus Register | Notes |
|---------------|-----------------|-------|
| 0-1 | 100 | Little-endian in assembly, big-endian in Modbus |
| 2-3 | 101 | |
| ... | ... | |
| 30-31 | 115 | Tool Weight (byte 30) and Tip Force (byte 31) |

### Configuration Assembly 151 → Modbus Holding Registers

- **Modbus Function**: Read/Write Holding Registers (0x03, 0x06, 0x10)
- **Register Range**: 150-154 (5 registers = 10 bytes)
- **Mapping**: Direct byte-to-register mapping

---

## Configuration and Byte Offsets

**Note:** MPU6050 data is fixed at byte offset 0-15 (16 bytes). The byte offset is not configurable in the current implementation.

---

## Thread Safety

All assembly data access is protected by a mutex (`sample_application_get_assembly_mutex()`). When reading or writing assembly data:

1. Take the mutex: `xSemaphoreTake(assembly_mutex, portMAX_DELAY)`
2. Perform read/write operations
3. Release the mutex: `xSemaphoreGive(assembly_mutex)`

**Note:** The mutex is shared across all components (MPU6050, Modbus TCP, EtherNet/IP).

---

## Example: Reading MPU6050 Data from Assembly 100

### C Code Example

```c
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

extern uint8_t g_assembly_data064[32];
extern SemaphoreHandle_t sample_application_get_assembly_mutex(void);

void read_mpu6050_data(int32_t *fused_angle, int32_t *cylinder1_pressure, 
                      int32_t *cylinder2_pressure, int32_t *temperature)
{
    SemaphoreHandle_t mutex = sample_application_get_assembly_mutex();
    if (mutex == NULL) return;
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    
    // Read 4 int32_t values (16 bytes) starting at byte 0
    // Use memcpy for proper little-endian handling
    memcpy(fused_angle, &g_assembly_data064[0], sizeof(int32_t));
    memcpy(cylinder1_pressure, &g_assembly_data064[4], sizeof(int32_t));
    memcpy(cylinder2_pressure, &g_assembly_data064[8], sizeof(int32_t));
    memcpy(temperature, &g_assembly_data064[12], sizeof(int32_t));
    
    xSemaphoreGive(mutex);
    
    // Convert scaled integers to physical units
    float fused_angle_deg = (float)*fused_angle / 100.0f;
    float cylinder1_psi = (float)*cylinder1_pressure / 10.0f;
    float cylinder2_psi = (float)*cylinder2_pressure / 10.0f;
    float temp_c = (float)*temperature / 100.0f;
}
```

### Python Example (via Modbus TCP)

```python
from pymodbus.client import ModbusTcpClient

client = ModbusTcpClient('192.168.1.100', port=502)
client.connect()

# Read Input Assembly 100 (Modbus registers 0-15)
result = client.read_input_registers(0, 16)

# Extract MPU6050 data (bytes 0-15)
# Registers 0-1 = Fused Angle (bytes 0-3, little-endian in assembly, big-endian in Modbus)
fused_angle_raw = (result.registers[1] << 16) | result.registers[0]
# Sign extend if negative (int32_t)
if fused_angle_raw & 0x80000000:
    fused_angle_raw = fused_angle_raw - 0x100000000
fused_angle_deg = fused_angle_raw / 100.0

# Registers 2-3 = Cylinder 1 Pressure (bytes 4-7)
cylinder1_raw = (result.registers[3] << 16) | result.registers[2]
if cylinder1_raw & 0x80000000:
    cylinder1_raw = cylinder1_raw - 0x100000000
cylinder1_psi = cylinder1_raw / 10.0

# Registers 4-5 = Cylinder 2 Pressure (bytes 8-11)
cylinder2_raw = (result.registers[5] << 16) | result.registers[4]
if cylinder2_raw & 0x80000000:
    cylinder2_raw = cylinder2_raw - 0x100000000
cylinder2_psi = cylinder2_raw / 10.0

# Registers 6-7 = Temperature (bytes 12-15)
temp_raw = (result.registers[7] << 16) | result.registers[6]
if temp_raw & 0x80000000:
    temp_raw = temp_raw - 0x100000000
temp_c = temp_raw / 100.0

print(f"Fused Angle: {fused_angle_deg:.2f}°")
print(f"Cylinder 1 Pressure: {cylinder1_psi:.1f} PSI")
print(f"Cylinder 2 Pressure: {cylinder2_psi:.1f} PSI")
print(f"Temperature: {temp_c:.2f}°C")

# Read Output Assembly 150 (Modbus registers 100-115)
result = client.read_holding_registers(100, 16)

# Tool Weight (byte 30 = register 115, low byte)
tool_weight = result.registers[15] & 0xFF

# Tip Force (byte 31 = register 115, high byte)
tip_force = (result.registers[15] >> 8) & 0xFF

print(f"Tool Weight: {tool_weight} lbs")
print(f"Tip Force: {tip_force} lbs")

client.close()
```

---

## Summary Table

| Assembly | Size | Primary Data Sources | Key Fields |
|----------|------|---------------------|------------|
| **100 (Input)** | 32 bytes | MPU6050 | Fused Angle, Cylinder Pressures, Temperature |
| **150 (Output)** | 32 bytes | EtherNet/IP controller | Tool Weight (byte 30), Tip Force (byte 31) |
| **151 (Config)** | 10 bytes | Reserved | TBD |

---

## Related Documentation

- [API Endpoints](API_Endpoints.md) - Web API for configuring byte offsets
- [Main README](../README.md) - Project overview
- [Modbus TCP Mapping](../README.md#modbus-tcp-mapping) - Modbus register mapping

---

**Last Updated**: See git commit history  
**Assembly Version**: 1.0

