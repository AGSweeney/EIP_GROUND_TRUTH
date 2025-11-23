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
| 0-19 | 20 bytes | MPU6050 IMU | Orientation and pressure data | 5 × int32_t (little-endian) |
| 20-29 | 10 bytes | Available | Reserved for other sensors | - |
| 30-31 | 2 bytes | Available | Reserved | - |

### MPU6050 IMU Data (Configurable Offset)

The MPU6050 sensor writes 20 bytes of data starting at a configurable byte offset (default: 0, configurable: 0-20).

**MPU6050 Data Structure (20 bytes total):**

| Byte Offset (within MPU6050 block) | Size | Field Name | Description | Format |
|-------------------------------------|------|------------|-------------|--------|
| 0-3 | 4 bytes | Roll | Roll angle (rotation around X-axis) | int32_t (degrees × 10000) |
| 4-7 | 4 bytes | Pitch | Pitch angle (rotation around Y-axis) | int32_t (degrees × 10000) |
| 8-11 | 4 bytes | Ground Angle | Absolute tilt from vertical | int32_t (degrees × 10000) |
| 12-15 | 4 bytes | Bottom Pressure | Bottom cylinder pressure | int32_t (PSI × 1000) |
| 16-19 | 4 bytes | Top Pressure | Top cylinder pressure | int32_t (PSI × 1000) |

**Example Values:**
- Roll: 12.34° = 123400 (0x0001E208)
- Pitch: -5.67° = -56700 (0xFFFFE6C4)
- Ground Angle: 13.45° = 134500 (0x00020D64)
- Bottom Pressure: 45.2 PSI = 45200 (0x0000B090)
- Top Pressure: 32.1 PSI = 32100 (0x00007D64)

**Note:** The MPU6050 byte offset is configurable via web API (`/api/mpu6050/byteoffset`). Valid offsets: 0-20 (must fit 20 bytes within 32-byte assembly).

### Reserved Space (Bytes 20-31)

Bytes 20-31 are available for other sensors or I/O data. Currently unused.

### Example Layout

```
Byte:  0    1    2    3    4    5    6    7    8    9    10   11   12   13   14   15
      ┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┐
      │ MPU6050 Roll (bytes 0-3)      │ MPU6050 Pitch (bytes 4-7)     │ MPU6050 Ground... │
      └────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┘

Byte:  16   17   18   19   20   21   22   23   24   25   26   27   28   29   30   31
      ┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┐
      │ MPU6050 Pressures (bytes 12-19)              │ Reserved (bytes 20-31)           │
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

**MPU6050 Angles:**
- Format: `int32_t` (degrees × 10000)
- Range: -214748.3648° to +214748.3647°
- Example: 12.34° = 123400
- Example: -5.67° = -56700

**MPU6050 Pressures:**
- Format: `int32_t` (PSI × 1000)
- Range: -2147483.648 PSI to +2147483.647 PSI
- Example: 45.2 PSI = 45200
- Example: 0.123 PSI = 123

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

### Configuring MPU6050 Byte Offset

**Via Web API:**
```bash
POST /api/mpu6050/byteoffset
{
  "start_byte": 0
}
```

**Valid Values:** 0-20 (must leave room for 20 bytes)

**Via NVS:**
- Stored in NVS key: `mpu6050_byte_start`
- Default: 0

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

void read_mpu6050_data(uint8_t byte_offset, int32_t *roll, int32_t *pitch, 
                      int32_t *ground_angle, int32_t *bottom_pressure, int32_t *top_pressure)
{
    SemaphoreHandle_t mutex = sample_application_get_assembly_mutex();
    if (mutex == NULL) return;
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    
    // Read 5 int32_t values (20 bytes) starting at byte_offset
    *roll = (int32_t)(g_assembly_data064[byte_offset + 0] |
                     (g_assembly_data064[byte_offset + 1] << 8) |
                     (g_assembly_data064[byte_offset + 2] << 16) |
                     (g_assembly_data064[byte_offset + 3] << 24));
    
    *pitch = (int32_t)(g_assembly_data064[byte_offset + 4] |
                      (g_assembly_data064[byte_offset + 5] << 8) |
                      (g_assembly_data064[byte_offset + 6] << 16) |
                      (g_assembly_data064[byte_offset + 7] << 24));
    
    *ground_angle = (int32_t)(g_assembly_data064[byte_offset + 8] |
                             (g_assembly_data064[byte_offset + 9] << 8) |
                             (g_assembly_data064[byte_offset + 10] << 16) |
                             (g_assembly_data064[byte_offset + 11] << 24));
    
    *bottom_pressure = (int32_t)(g_assembly_data064[byte_offset + 12] |
                                 (g_assembly_data064[byte_offset + 13] << 8) |
                                 (g_assembly_data064[byte_offset + 14] << 16) |
                                 (g_assembly_data064[byte_offset + 15] << 24));
    
    *top_pressure = (int32_t)(g_assembly_data064[byte_offset + 16] |
                             (g_assembly_data064[byte_offset + 17] << 8) |
                             (g_assembly_data064[byte_offset + 18] << 16) |
                             (g_assembly_data064[byte_offset + 19] << 24));
    
    xSemaphoreGive(mutex);
    
    // Convert scaled integers to physical units
    float roll_deg = (float)*roll / 10000.0f;
    float pitch_deg = (float)*pitch / 10000.0f;
    float ground_angle_deg = (float)*ground_angle / 10000.0f;
    float bottom_pressure_psi = (float)*bottom_pressure / 1000.0f;
    float top_pressure_psi = (float)*top_pressure / 1000.0f;
}
```

### Python Example (via Modbus TCP)

```python
from pymodbus.client import ModbusTcpClient

client = ModbusTcpClient('192.168.1.100', port=502)
client.connect()

# Read Input Assembly 100 (Modbus registers 0-15)
result = client.read_input_registers(0, 16)

# Extract MPU6050 data (assuming byte offset 0)
# Registers 0-1 = Roll (bytes 0-3)
roll_raw = (result.registers[1] << 16) | result.registers[0]
roll_deg = roll_raw / 10000.0

# Registers 2-3 = Pitch (bytes 4-7)
pitch_raw = (result.registers[3] << 16) | result.registers[2]
pitch_deg = pitch_raw / 10000.0

# Registers 4-5 = Ground Angle (bytes 8-11)
ground_angle_raw = (result.registers[5] << 16) | result.registers[4]
ground_angle_deg = ground_angle_raw / 10000.0

# Registers 6-7 = Bottom Pressure (bytes 12-15)
bottom_pressure_raw = (result.registers[7] << 16) | result.registers[6]
bottom_pressure_psi = bottom_pressure_raw / 1000.0

# Registers 8-9 = Top Pressure (bytes 16-19)
top_pressure_raw = (result.registers[9] << 16) | result.registers[8]
top_pressure_psi = top_pressure_raw / 1000.0

print(f"Roll: {roll_deg:.2f}°")
print(f"Pitch: {pitch_deg:.2f}°")
print(f"Ground Angle: {ground_angle_deg:.2f}°")
print(f"Bottom Pressure: {bottom_pressure_psi:.2f} PSI")
print(f"Top Pressure: {top_pressure_psi:.2f} PSI")

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
| **100 (Input)** | 32 bytes | MPU6050 | Roll, Pitch, Ground Angle, Pressures |
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

