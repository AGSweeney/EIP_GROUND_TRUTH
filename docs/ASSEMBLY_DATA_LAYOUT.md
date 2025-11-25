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
| 0-19 | 20 bytes | IMU Sensor | Roll, pitch, ground angle, cylinder pressures | 5 × int32_t (little-endian) |
| 20-31 | 12 bytes | Available | Reserved for other sensors | - |

**Note:** The IMU sensor (MPU6050 primary, LSM6DS3 fallback) uses a configurable byte offset (default: 0). The byte offset can be configured via API endpoints to avoid conflicts with other sensors.

### IMU Sensor Data (MPU6050 or LSM6DS3)

The IMU sensor writes 20 bytes of data at a configurable byte offset (default: byte 0). The system automatically detects and uses MPU6050 if available, otherwise falls back to LSM6DS3.

**IMU Data Structure (20 bytes total, 5 × int32_t):**

| Byte Offset | Size | Field Name | Description | Format |
|-------------|------|------------|-------------|--------|
| 0-3 | 4 bytes | Roll | Roll angle from horizontal | int32_t (degrees × 10000) |
| 4-7 | 4 bytes | Pitch | Pitch angle from horizontal | int32_t (degrees × 10000) |
| 8-11 | 4 bytes | Ground Angle | Angle from vertical | int32_t (degrees × 10000) |
| 12-15 | 4 bytes | Bottom Pressure | Bottom cylinder pressure | int32_t (PSI × 1000) |
| 16-19 | 4 bytes | Top Pressure | Top cylinder pressure | int32_t (PSI × 1000) |

**Example Values:**
- Roll: 12.3456° = 123456 (0x0001E240)
- Pitch: -5.6789° = -56789 (0xFFFEECEB)
- Ground Angle: 13.4500° = 134500 (0x00020C49)
- Bottom Pressure: 123.456 PSI = 123456 (0x0001E240)
- Top Pressure: 87.650 PSI = 87650 (0x00015602)

**Note:** 
- Both MPU6050 and LSM6DS3 use sensor fusion (complementary filter) combining accelerometer and gyroscope data
- Cylinder pressures are calculated based on roll/pitch angles, tool weight, tip force, and cylinder bore from Output Assembly 150
- Data is written continuously at 50Hz (20ms update rate)
- The byte offset is configurable via `/api/mpu6050/byteoffset` or `/api/lsm6ds3/byteoffset` API endpoints
- Valid byte offsets: 0-12 (ensures 20-byte block fits within 32-byte assembly)

### Example Layout (Default: Byte Offset 0)

```
Byte:  0    1    2    3    4    5    6    7    8    9    10   11   12   13   14   15
      ┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┐
      │ Roll (bytes 0-3)                │ Pitch (bytes 4-7)                               │
      └────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┘

Byte:  16   17   18   19   20   21   22   23   24   25   26   27   28   29   30   31
      ┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┐
      │ Ground Angle (bytes 8-11)       │ Bottom Pressure (bytes 12-15)                  │
      └────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┘

Byte:  20-31: Top Pressure (bytes 16-19) + Reserved space
```

---

## Assembly 150 (Output Assembly) - 32 Bytes

The Output Assembly contains control data and output states sent from the EtherNet/IP controller.

### Fixed Layout

| Byte Range | Size | Field Name | Description | Format |
|------------|------|------------|-------------|--------|
| 0-28 | 29 bytes | Control Data | General control and output data | Configurable |
| 29 | 1 byte | Cylinder Bore | Cylinder bore diameter (scaled by 100) | uint8_t (0.01-2.55 inches) |
| 30 | 1 byte | Tool Weight | Tool weight in pounds | uint8_t (0-255 lbs) |
| 31 | 1 byte | Tip Force | Desired tip force in pounds | uint8_t (0-255 lbs) |

### Cylinder Bore, Tool Weight, and Tip Force (Bytes 29-31)

These values are used by the IMU sensor fusion algorithm (MPU6050 or LSM6DS3) to calculate cylinder pressures:

- **Cylinder Bore (Byte 29)**: Cylinder bore diameter in inches (scaled by 100)
  - Format: `uint8_t` scaled by 100 (0 = use NVS, 1-255 = 0.01-2.55 inches)
  - Default: 1.0 inch (if byte is 0, falls back to NVS stored value)
  - Conversion: `bore_inches = byte_value / 100.0`
  - Examples:
    - `100` = 1.00 inch bore
    - `113` = 1.13 inch bore (1 square inch area)
    - `200` = 2.00 inch bore
    - `255` = 2.55 inch bore (maximum)
  - Used to calculate cylinder area: `area = π × (bore/2)² = π × bore² / 4`

- **Tool Weight (Byte 30)**: Weight of the tool in pounds (0-255 lbs)
  - Default: 50 lbs (if byte is 0, falls back to NVS stored value)
  - Used to calculate gravity component: `gravity = tool_weight × cos(angle_from_vertical)`

- **Tip Force (Byte 31)**: Desired tip force in pounds (0-255 lbs)
  - Default: 20 lbs (if byte is 0, falls back to NVS stored value)
  - Used in force balance equation: `top_force = tip_force - gravity_component + bottom_force`

**Note:** If any byte is 0 (or out of range for cylinder bore), the system falls back to values stored in NVS (Non-Volatile Storage).

### Example Layout

```
Byte:  0    1    2    3    4    5    6    7    8    9    10   11   12   13   14   15
      ┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┐
      │ Control Data (bytes 0-29) - Available for application use                      │
      └────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┘

Byte:  16   17   18   19   20   21   22   23   24   25   26   27   28   29   30   31
      ┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┐
      │ Control Data (continued)                    │Bore │ Tool │ Tip │
      │                                               │     │Weight│Force│
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

**IMU Orientation Angles (Roll, Pitch, Ground Angle):**
- Format: `int32_t` (degrees × 10000)
- Range: -214748.3648° to +214748.3647°
- Precision: 0.0001° (0.1 milli-degrees)
- Example: 12.3456° = 123456
- Example: -5.6789° = -56789

**IMU Cylinder Pressures (Bottom Pressure, Top Pressure):**
- Format: `int32_t` (PSI × 1000)
- Range: -2147483.648 PSI to +2147483.647 PSI
- Precision: 0.001 PSI (1 milli-PSI)
- Example: 123.456 PSI = 123456
- Example: 87.650 PSI = 87650

**Cylinder Bore:**
- Format: `uint8_t` (scaled by 100)
- Range: 0 = use NVS, 1-255 = 0.01-2.55 inches
- Example: 1.00 inch = 100 (0x64)
- Example: 1.13 inch = 113 (0x71)
- Example: 2.00 inch = 200 (0xC8)

**Tool Weight / Tip Force:**
- Format: `uint8_t` (pounds)
- Range: 0-255 lbs (0 = use NVS)
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
| 29 | 114 (low byte) | Cylinder Bore (byte 29) |
| 30-31 | 115 | Tool Weight (byte 30) and Tip Force (byte 31) |

### Configuration Assembly 151 → Modbus Holding Registers

- **Modbus Function**: Read/Write Holding Registers (0x03, 0x06, 0x10)
- **Register Range**: 150-154 (5 registers = 10 bytes)
- **Mapping**: Direct byte-to-register mapping

---

## Configuration and Byte Offsets

**IMU Sensor Byte Offset:**
- Both MPU6050 and LSM6DS3 use 20 bytes (5 × int32_t)
- Byte offset is configurable via API endpoints:
  - `/api/mpu6050/byteoffset` (GET/POST) for MPU6050
  - `/api/lsm6ds3/byteoffset` (GET/POST) for LSM6DS3
- Valid byte offsets: 0-12 (ensures 20-byte block fits within 32-byte assembly)
- Default: 0 (bytes 0-19)
- The system automatically detects which sensor is available and uses the appropriate configuration

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

# Cylinder Bore (byte 29 = register 114, low byte)
cylinder_bore_byte = result.registers[14] & 0xFF
cylinder_bore_inches = cylinder_bore_byte / 100.0 if cylinder_bore_byte > 0 else 1.0  # Default to 1.0 if 0

# Tool Weight (byte 30 = register 115, low byte)
tool_weight = result.registers[15] & 0xFF

# Tip Force (byte 31 = register 115, high byte)
tip_force = (result.registers[15] >> 8) & 0xFF

print(f"Cylinder Bore: {cylinder_bore_inches:.2f} inches")
print(f"Tool Weight: {tool_weight} lbs")
print(f"Tip Force: {tip_force} lbs")

client.close()
```

---

## Summary Table

| Assembly | Size | Primary Data Sources | Key Fields |
|----------|------|---------------------|------------|
| **100 (Input)** | 32 bytes | MPU6050 | Fused Angle, Cylinder Pressures, Temperature |
| **150 (Output)** | 32 bytes | EtherNet/IP controller | Cylinder Bore (byte 29), Tool Weight (byte 30), Tip Force (byte 31) |
| **151 (Config)** | 10 bytes | Reserved | TBD |

---

## Related Documentation

- [API Endpoints](API_Endpoints.md) - Web API for configuring byte offsets
- [Main README](../README.md) - Project overview
- [Modbus TCP Mapping](../README.md#modbus-tcp-mapping) - Modbus register mapping

---

**Last Updated**: See git commit history  
**Assembly Version**: 1.0

