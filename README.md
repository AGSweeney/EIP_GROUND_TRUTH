# ESP32-P4 OpENer Integration

## Overview
- Port of the OpENer EtherNet/IP™ stack to Espressif’s ESP32-P4 using ESP-IDF v5.5
- Runs the canonical sample application with cyclic I/O assemblies and class 1/3 messaging support
- Integrates with `esp_netif` and the ESP32 Ethernet MAC/PHY driver for link management and address assignment
- Implements static-IP Address Conflict Detection (ACD) for EtherNet/IP adapters while keeping DHCP compatibility

## Active Components

The following components are actively used in this project:

- **opener** - OpENer EtherNet/IP stack
- **webui** - Web user interface for configuration and monitoring
- **modbus_tcp** - Modbus TCP server implementation
- **system_config** - System configuration management (NVS)
- **ota_manager** - Over-the-air firmware update support
- **log_buffer** - System log capture and retrieval (circular buffer)
- **vl53l1x_uld** - VL53L1X sensor driver (Ultra-Lite Driver)
- **vl53l1x_config** - VL53L1X configuration management
- **mcp23017** - MCP23017 16-bit I/O expander driver
- **mcp23008** - MCP23008 8-bit I/O expander driver
- **mpu6050** - MPU6050 6-axis motion sensor driver (accelerometer, gyroscope)

**Note:** Additional peripheral component libraries (BMI270, BNO086, etc.) are available in `components/unused/` but are not currently integrated or tested. See [components/unused/README.md](components/unused/README.md) for details.

## VL53L1X Sensor Integration

The VL53L1X ToF sensor is integrated into the EtherNet/IP input assembly, providing real-time distance measurements and sensor status information to connected PLCs.

**✅ Compatibility Note**: The VL53L1X sensor and MCP230XX I/O expanders have been tested together on the same I2C bus and confirmed to work correctly. Both devices share the I2C bus (using different addresses) and operate simultaneously without conflicts. I2C bus contention is handled automatically with retry logic and exponential backoff.

### Sensor Configuration

- **I2C Interface**: Configurable via Kconfig (`CONFIG_OPENER_I2C_SCL_GPIO`, `CONFIG_OPENER_I2C_SDA_GPIO`) – defaults: SCL **GPIO 8**, SDA **GPIO 7**
- **Default I2C Address**: `0x29`
- **Update Rate**: 10 Hz (100ms intervals)
- **Distance Mode**: Long range (up to 4 meters)
- **Task Core**: Core 1 (OpENer and lwIP run on Core 0)

### Sensor Enable/Disable

The VL53L1X sensor can be enabled or disabled at runtime via the web interface:
- When **enabled**: Sensor task runs and writes data to Input Assembly 100
- When **disabled**: Sensor task stops, and the configured byte range in Input Assembly 100 is zeroed out
- The sensor enable state persists across reboots (stored in NVS)
- The VL53L1x status page is hidden from navigation when the sensor is disabled

### Startup Behaviour When the Sensor Is Missing

- If the VL53L1X probe sequence fails (for example, the sensor is absent or wired incorrectly), the ToF task logs `VL53L1x initialization failed`, cleans up, and exits.
- The configured sensor data byte range in Input Assembly 100 remains at zero because no measurements are produced after the task stops.
- All other EtherNet/IP functionality—TCP/IP, assemblies, Connection Manager diagnostics, and GPIO33 LED control—continues to operate normally.
- Restoring sensor functionality requires fixing the hardware issue and rebooting so the initialization task can run again.

### Input Assembly Byte Layout

Sensor data is written to a configurable byte range in Input Assembly 100 (`g_assembly_data064`) in little-endian format. The default location is bytes 0-8, but can be configured to bytes 9-17 or 18-26 via the web interface:

| Byte(s) | Data Type | Description | Units | Valid Range |
|---------|-----------|-------------|-------|-------------|
| 0-1 | `uint16_t` | **Distance** | millimeters (mm) | 0-4000 mm (0 = no target) |
| 2 | `uint8_t` | **Status** | Range status code | 0 = valid, see status codes below |
| 3-4 | `uint16_t` | **Ambient** | Ambient light level | kcps (kilo counts per second) |
| 5-6 | `uint16_t` | **SigPerSPAD** | Signal per SPAD | kcps/SPAD |
| 7-8 | `uint16_t` | **NumSPADs** | Number of enabled SPADs | count (typically 16-64) |

**Note:** The sensor data byte range is configurable (0-8, 9-17, or 18-26). Bytes outside the configured range are available for other application data and are not overwritten by the sensor task. When the sensor byte offset is changed, the old byte range is automatically zeroed out.

### Range Status Codes

| Code | Description |
|------|-------------|
| 0 | No error (valid measurement) |
| 1 | Sigma failed (measurement uncertainty too high) |
| 2 | Signal failed (signal too weak) |
| 3 | Target out of range |
| 4 | Signal failed |
| 5 | Range valid but wrapped |
| 6 | Target out of range |
| 7 | Wrap-around (target beyond max range) |
| 9-13 | Range valid but wrapped |
| 255 | Invalid/unknown status |

### PLC Integration Example (Allen-Bradley Micro850)

For Structured Text conversion from SINT array to sensor variables:

```structured_text
PROGRAM VL53L1x_Sensor_Data
VAR
    ESP32p4_I : ARRAY[0..31] OF SINT;
    Distance_mm : UINT;
    Status : UINT;
    Ambient_kcps : UINT;
    SigPerSPAD_kcps : UINT;
    NumSPADs : UINT;
    ByteLow : BYTE;
    ByteHigh : BYTE;
END_VAR

// Convert Distance (bytes 0-1, little-endian)
ByteLow := ANY_TO_BYTE(ESP32p4_I[0]);
ByteHigh := ANY_TO_BYTE(ESP32p4_I[1]);
Distance_mm := ANY_TO_UINT(ByteLow) + ((ANY_TO_UINT(ByteHigh) * 256));

// Extract Status (byte 2)
ByteLow := ANY_TO_BYTE(ESP32p4_I[2]);
Status := ANY_TO_UINT(ByteLow);

// Convert Ambient (bytes 3-4, little-endian)
ByteLow := ANY_TO_BYTE(ESP32p4_I[3]);
ByteHigh := ANY_TO_BYTE(ESP32p4_I[4]);
Ambient_kcps := ANY_TO_UINT(ByteLow) + ((ANY_TO_UINT(ByteHigh) * 256));

// Convert SigPerSPAD (bytes 5-6, little-endian)
ByteLow := ANY_TO_BYTE(ESP32p4_I[5]);
ByteHigh := ANY_TO_BYTE(ESP32p4_I[6]);
SigPerSPAD_kcps := ANY_TO_UINT(ByteLow) + ((ANY_TO_UINT(ByteHigh) * 256));

// Convert NumSPADs (bytes 7-8, little-endian)
ByteLow := ANY_TO_BYTE(ESP32p4_I[7]);
ByteHigh := ANY_TO_BYTE(ESP32p4_I[8]);
NumSPADs := ANY_TO_UINT(ByteLow) + ((ANY_TO_UINT(ByteHigh) * 256));
END_PROGRAM
```

### Sensor Data Interpretation

- **Distance_mm**: Valid range is typically 50-4000 mm. Values of 0 usually indicate no target detected or out of range.
- **Status**: Always check `Status = 0` before using distance values. Non-zero values indicate measurement errors or out-of-range conditions.
- **Ambient_kcps**: Higher values indicate brighter ambient light, which may affect measurement accuracy.
- **SigPerSPAD_kcps**: Higher values indicate stronger return signal. Values > 100 kcps/SPAD typically indicate good signal quality.
- **NumSPADs**: Number of active SPADs used for measurement. Typically ranges from 16-64 depending on configuration.

For detailed sensor API documentation, see [components/vl53l1x_uld/README.md](components/vl53l1x_uld/README.md).

## MCP230XX I/O Expander Integration

The ESP32-P4 OpENer integration supports MCP23017 (16-bit) and MCP23008 (8-bit) I/O expanders via I2C, providing digital input/output capabilities that are mapped to EtherNet/IP assemblies for PLC control and monitoring.

**✅ Compatibility Note**: The MCP230XX I/O expanders and VL53L1X sensor have been tested together on the same I2C bus and confirmed to work correctly. Both devices share the I2C bus (using different addresses) and operate simultaneously without conflicts. I2C bus contention is handled automatically with retry logic and exponential backoff.

### Supported Devices

- **MCP23017**: 16-bit I/O expander (2 ports: Port A and Port B)
  - I2C address range: 0x20-0x27 (configurable via A0, A1, A2 pins)
  - Default address: 0x20
  - 16 digital I/O pins (8 per port)
  
- **MCP23008**: 8-bit I/O expander (1 port)
  - I2C address range: 0x20-0x27 (configurable via A0, A1, A2 pins)
  - Default address: 0x20
  - 8 digital I/O pins

### Multiple Device Support

The system supports multiple MCP devices simultaneously:
- Up to **8 devices** can be configured (limited by available I2C addresses)
- Each device can be independently configured as inputs or outputs
- Devices share the same I2C bus (configurable via Kconfig)
- Boot-time device detection automatically scans for connected devices
- Stale configurations (devices no longer present) are automatically cleaned up

### I2C Configuration

- **I2C Interface**: Configurable via Kconfig (`CONFIG_OPENER_I2C_SCL_GPIO`, `CONFIG_OPENER_I2C_SDA_GPIO`) – defaults: SCL **GPIO 8**, SDA **GPIO 7**
- **I2C Speed**: 400 kHz
- **Bus Sharing**: MCP devices can share the I2C bus with other devices (e.g., VL53L1X sensor)

### Device Configuration

MCP devices are configured via the web interface:

#### Main Configuration Page (`/`)

- **Enable/Disable**: Enable or disable MCP230XX I/O Expander support system-wide
- **I/O Task Update Rate**: Configure the task update rate to match your PLC's RPI
  - Default: 20ms (50 Hz)
  - Options: 10ms (100 Hz), 20ms (50 Hz), 50ms (20 Hz), 100ms (10 Hz), 200ms (5 Hz), 500ms (2 Hz), 1000ms (1 Hz)
  - **Important**: Match this to your PLC's RPI setting for optimal synchronization
  - Changes require a restart to take effect

#### Device-Specific Configuration (`/mcpconfig`)

- **Device Type**: Select MCP23017 (16-bit) or MCP23008 (8-bit)
- **Enable/Disable**: Enable or disable individual devices
- **Pin Direction**: Configure all pins as either inputs or outputs (device-level configuration)
  - **Output**: All pins configured as outputs (IODIR = 0x0000 for MCP23017, 0x00 for MCP23008)
  - **Input**: All pins configured as inputs (IODIR = 0xFFFF for MCP23017, 0xFF for MCP23008)
- **Output Logic Inversion**: Invert output logic for active-low devices (e.g., LED cathodes connected to outputs)
  - **Standard**: Normal logic (1 = high, 0 = low)
  - **Reversed**: Inverted logic (1 = low, 0 = high)

#### EtherNet/IP Byte Mapping

Each device maps to a configurable byte range in the EtherNet/IP assemblies:

- **Input Devices**: Map to Input Assembly 100 (`g_assembly_data064`, 32 bytes)
  - MCP23017 uses 2 bytes (Port A = byte 0, Port B = byte 1)
  - MCP23008 uses 1 byte
  - Byte offset is configurable (0-31 for MCP23017, 0-31 for MCP23008)
  
- **Output Devices**: Map to Output Assembly 150 (`g_assembly_data096`, 32 bytes)
  - MCP23017 uses 2 bytes (Port A = byte 0, Port B = byte 1)
  - MCP23008 uses 1 byte
  - Byte offset is configurable (0-31 for MCP23017, 0-31 for MCP23008)

**Byte Range Overlap Detection**: The web interface prevents selecting overlapping byte ranges between devices. Overlapping ranges are automatically disabled in the dropdown menus, and overlap warnings are displayed if conflicts are detected.

### Boot-Time Behavior

- **Device Detection**: On boot, the system scans the I2C bus for MCP devices at addresses 0x20-0x27
- **Configuration Loading**: Saved device configurations are loaded from NVS
- **IODIR Configuration**: IODIR registers are configured based on saved pin direction settings
- **Stale Configuration Cleanup**: Configurations for devices that are no longer detected are automatically removed from NVS
- **Task Startup**: The MCP I/O task starts automatically if MCP is enabled and at least one device is detected

### I/O Task Operation

The MCP I/O task runs on Core 1 with a configurable update rate to match your PLC's RPI (Requested Packet Interval):

- **Update Rate**: Configurable via web interface (default: 20ms / 50 Hz)
  - Available options: 10ms (100 Hz), 20ms (50 Hz), 50ms (20 Hz), 100ms (10 Hz), 200ms (5 Hz), 500ms (2 Hz), 1000ms (1 Hz)
  - Recommended: Match your PLC's RPI setting (typically 10ms, 20ms, 50ms, or 100ms)
  - Configuration persists in NVS and requires a restart to take effect
- **Input Devices**: Reads GPIO values and writes to Input Assembly 100 at the configured byte offset
- **Output Devices**: Reads from Output Assembly 150 at the configured byte offset and writes to OLAT registers
- **Output Logic Inversion**: Applied automatically for devices configured with inverted output logic
- **Mutex Protection**: All assembly access is protected by mutexes for thread-safe operation

### Data Format

#### MCP23017 (16-bit)

- **Little-Endian Format**: Port A = low byte, Port B = high byte
- **Input Assembly**: `gpio_data[0] = Port A, gpio_data[1] = Port B`
- **Output Assembly**: `output_data[0] = Port A, output_data[1] = Port B`

#### MCP23008 (8-bit)

- **Single Byte Format**: All 8 pins in one byte
- **Input Assembly**: `gpio_data[0] = GPIO port value`
- **Output Assembly**: `output_data[0] = GPIO port value`

### Web Interface Access

- **Main Configuration Page**: Enable/disable MCP I/O Expander support
- **MCP Configuration Page**: `/mcpconfig` - Configure individual devices, pin directions, byte mappings, and output logic inversion
- **Device Selector**: When multiple devices are detected, a dropdown menu allows selecting which device to configure

### Configuration Persistence

All MCP configurations are stored in NVS and persist across reboots:

**System-Level Settings:**
- MCP enable/disable state
- I/O task update rate (default: 20ms / 50 Hz)

**Device-Level Settings:**
- Device type (MCP23017 or MCP23008)
- Enable/disable state per device
- Pin direction (input/output)
- Input/output byte offsets
- Output logic inversion setting

**Note**: Device type and update rate changes require a device reboot to take effect.

## MPU6050 IMU Integration

The ESP32-P4 OpENer integration includes a complete driver library for the InvenSense MPU6050 motion sensor:

### Supported Device

- **MPU6050**: 6-axis IMU (3-axis accelerometer + 3-axis gyroscope)
  - I2C address: `0x68` (AD0 LOW) or `0x69` (AD0 HIGH)
  - Configurable accelerometer ranges: ±2G, ±4G, ±8G, ±16G
  - Configurable gyroscope ranges: ±250, ±500, ±1000, ±2000 DPS
  - Digital Low-Pass Filter: 5Hz to 260Hz bandwidth
  - Temperature sensor included

### Features

The library provides:
- **Complete I2C register access** - Low-level register read/write functions
- **High-level sensor reading** - Read accelerometer, gyroscope, and temperature data
- **Orientation calculations** - Calculate roll, pitch, and absolute ground angle from accelerometer data
- **Configurable sensor ranges** - Adjust full-scale ranges for your application
- **Digital Low-Pass Filtering** - Configurable bandwidth for noise reduction
- **Sample rate control** - Adjustable output data rate (up to 1kHz)
- **Default configuration** - One-call setup with sensible defaults

### Orientation and Ground Angle Calculations

Both libraries include functions to calculate orientation from accelerometer data:

- **Roll**: Rotation around X-axis (degrees)
- **Pitch**: Rotation around Y-axis (degrees)
- **Absolute Ground Angle**: Total tilt angle relative to horizontal plane, calculated as `sqrt(roll² + pitch²)` (degrees)

The absolute ground angle is useful for applications that need to know the overall tilt regardless of direction (e.g., leveling systems, tilt alarms).

### I2C Configuration

- **I2C Interface**: Shares the same I2C bus as other peripherals (configurable via Kconfig)
- **I2C Speed**: Supports standard speeds (typically 400kHz)
- **Bus Sharing**: Can share I2C bus with VL53L1X, MCP230XX, and other devices

### Usage Example

```c
#include "mpu6050.h"
#include "driver/i2c_master.h"

// Initialize I2C device handle
i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = MPU6050_I2C_ADDR_PRIMARY,
    .scl_speed_hz = 400000,
};

i2c_master_dev_handle_t i2c_dev;
i2c_master_bus_add_device(i2c_bus, &dev_cfg, &i2c_dev);

// Initialize sensor
mpu6050_t imu;
mpu6050_init(&imu, i2c_dev);
ESP_ERROR_CHECK(mpu6050_configure_default(&imu));

// Read sensor data
mpu6050_sample_t sample;
ESP_ERROR_CHECK(mpu6050_read_all(&imu, &sample));

// Calculate orientation
mpu6050_accel_t accel;
ESP_ERROR_CHECK(mpu6050_read_accel(&imu, &accel));

mpu6050_orientation_t orientation;
ESP_ERROR_CHECK(mpu6050_calculate_orientation(&imu, &accel, &orientation));

ESP_LOGI("IMU", "Roll: %.2f°, Pitch: %.2f°, Ground Angle: %.2f°",
         orientation.roll, orientation.pitch, orientation.abs_ground_angle);
```

### Documentation

For complete API documentation and usage examples, see:
- **[MPU6050 Library Documentation](components/mpu6050/README.md)**

**Note**: The MPU6050 library is integrated into the EtherNet/IP assemblies and web interface. IMU data is exposed through Input Assembly 100 and can be configured via the web UI.

## Web Configuration Interface

The device includes a comprehensive web-based user interface (HTTP port 80) providing:

- **Network Configuration**: Configure DHCP/Static IP, netmask, gateway, and DNS settings
- **VL53L1x Sensor Configuration**: Full sensor parameter configuration with calibration support
- **Real-time Sensor Monitoring**: Live distance readings with visual bar chart
- **EtherNet/IP Assembly Monitoring**: Bit-level visualization of Input and Output assemblies
- **Modbus TCP Configuration**: Enable/disable Modbus TCP server
- **OTA Firmware Updates**: Upload and install firmware updates via web interface
- **System Logs Access**: Retrieve boot logs and runtime logs via REST API

### Accessing the Web Interface

- **URL**: `http://<device-ip-address>` (e.g., `http://192.168.1.100`)
- **Port**: 80 (HTTP)
- **Navigation**: Top menu bar provides access to:
  - **Configuration** (`/`) - Device settings and configuration
  - **VL53L1x** (`/vl53l1x`) - Sensor status and monitoring (only visible when sensor is enabled)
  - **MCP230XX I/O Expander** (`/mcpconfig`) - MCP device configuration (only visible when MCP is enabled)
  - **T->O** (`/inputassembly`) - Input Assembly (Target to Originator) monitoring
  - **O->T** (`/outputassembly`) - Output Assembly (Originator to Target) monitoring
  - **Firmware Update** (`/ota`) - Over-the-air firmware updates

### Key Features

- **Network Configuration**: Configure IP settings via web UI (stored in OpENer's NVS)
- **Sensor Enable/Disable**: Enable or disable the VL53L1x sensor at runtime
- **Sensor Data Byte Range**: Configure sensor data location (bytes 0-8, 9-17, or 18-26)
- **MCP230XX I/O Expander Configuration**: Configure multiple MCP devices, pin directions, byte mappings, and output logic inversion
- **Real-time Monitoring**: Live sensor readings with auto-refresh every 250ms
- **Bit-level Assembly Visualization**: View Input and Output assemblies byte-by-byte with individual bit checkboxes
- **Modbus TCP Control**: Enable/disable Modbus TCP server via web interface
- **No External Dependencies**: All CSS and JavaScript is self-contained (no CDN required)

### Complete Documentation

For detailed information about the web interface, including screenshots, API endpoints, and usage instructions, see:

**[📖 Web UI User Guide](docs/WebUIReadme.md)**

The Web UI User Guide includes:
- Complete page descriptions with screenshots
- Step-by-step configuration instructions
- REST API reference with examples
- Troubleshooting guide
- Browser compatibility information

## Modbus TCP Implementation

The device includes a Modbus TCP server implementation that provides access to EtherNet/IP assembly data via the standard Modbus protocol.

### Modbus TCP Server

- **Port**: 502 (standard Modbus TCP port)
- **Protocol**: Modbus TCP/IP (Modbus over TCP)
- **Max Connections**: 5 concurrent clients
- **Endianness**: Big-endian (Modbus standard)
- **Enable/Disable**: Can be enabled or disabled via web interface (Configuration page)
- **Configuration**: Settings persist across reboots (stored in NVS)

### Register Mapping

The Modbus register map mirrors the EtherNet/IP assemblies:

#### Input Registers (Read-Only)
- **Address Range**: 0-15 (16 registers = 32 bytes)
- **Maps to**: Input Assembly 100 (`g_assembly_data064`)
- **Function Code**: 04 (Read Input Registers)

**Register Layout:**
| Register | Assembly Bytes | Description |
|----------|----------------|-------------|
| 0 | 0-1 | Distance (mm), 16-bit value |
| 1 | 2-3 | Status (byte 2) + Ambient low byte (byte 3) |
| 2 | 4-5 | Ambient high byte (byte 4) + Signal per SPAD low (byte 5) |
| 3 | 6-7 | Signal per SPAD high (byte 6) + Number of SPADs low (byte 7) |
| 4 | 8-9 | Number of SPADs high (byte 8) + Reserved (byte 9) |
| 5-15 | 10-31 | Reserved/application data |

**Note:** The mapping is byte-aligned, so some sensor fields span multiple registers. For convenience, you can read registers 0-4 to get all sensor data (distance, status, ambient, signal per SPAD, number of SPADs).

#### Holding Registers (Read-Write)

**Output Assembly Mapping (Registers 100-115)**
- **Address Range**: 100-115 (16 registers = 32 bytes)
- **Maps to**: Output Assembly 150 (`g_assembly_data096`)
- **Function Codes**: 03 (Read Holding Registers), 06 (Write Single Register), 16 (Write Multiple Registers)

**Configuration Assembly Mapping (Registers 150-154)**
- **Address Range**: 150-154 (5 registers = 10 bytes)
- **Maps to**: Configuration Assembly 151 (`g_assembly_data097`)
- **Function Codes**: 03 (Read Holding Registers), 06 (Write Single Register), 16 (Write Multiple Registers)

### Endianness Conversion

The implementation automatically handles endianness conversion:
- **EtherNet/IP Assemblies**: Store data in little-endian format (LSB first)
- **Modbus TCP**: Transmits data in big-endian format (MSB first)
- **Conversion**: Performed automatically during read/write operations

### Example Modbus Client Usage

#### Reading Distance (Input Register 0)
```python
from pymodbus.client import ModbusTcpClient

client = ModbusTcpClient('192.168.1.100', port=502)
client.connect()

# Read distance (register 0, 16-bit value in big-endian format)
result = client.read_input_registers(0, 1, unit=1)
if not result.isError():
    # pymodbus automatically converts big-endian bytes to integer
    distance = result.registers[0]
    print(f"Distance: {distance} mm")
```

#### Writing LED Control (Holding Register 100)
```python
# Write to Output Assembly 150, bit 0 (LED control)
# Register 100 = first 16 bits of Output Assembly 150
result = client.write_register(100, 0x0001, unit=1)  # Turn LED on
```

### Supported Modbus Function Codes

| Function Code | Name | Description | Supported |
|---------------|------|-------------|-----------|
| 03 | Read Holding Registers | Read holding registers (100-115, 150-154) | Yes |
| 04 | Read Input Registers | Read input registers (0-15) | Yes |
| 06 | Write Single Register | Write single holding register | Yes |
| 16 | Write Multiple Registers | Write multiple holding registers | Yes |

### Thread Safety

All register access is protected by mutexes to ensure thread-safe operation when accessed concurrently from:
- EtherNet/IP cyclic I/O connections
- Modbus TCP clients
- Internal sensor task updates

## Enabled EtherNet/IP Objects
- **Class 0x02 – Message Router**  
  Core message dispatch services for all explicit requests.
- **Class 0x01 – Identity**  
  Full support for state transitions (Startup → Standby → Operational), recoverable/unrecoverable fault flags, Run/Idle header control bits, device reset service (Type 0 and Type 1), and the standard identity attributes (Vendor ID `55512`, Device Type `7`, Product Code `1`, Product Name `ESP32P4-EIP`).
- **Class 0xF5 – TCP/IP Interface**  
  DHCP/static configuration, multicast settings (attribute 9), encapsulation inactivity timeout (attribute 13), and persistence through NVS.
- **Class 0xF6 – Ethernet Link**  
  Negotiated speed/duplex reporting, physical MAC address, interface and media counters, interface type/state, and optional admin control.
- **Class 0x06 – Connection Manager**  
  Enables class 1 cyclic I/O and class 3 explicit messaging channels. Attribute 11 (CPU Utilization) is intentionally fixed at `0` on this platform because FreeRTOS statistics fluctuate too much for a reliable percentage. Buffer attributes 12/13 report the static 4096‑byte defaults used by OpENer.
- **Class 0x04 – Assemblies**  
  Input (`100`), output (`150`), and configuration (`151`) data sets for the sample application.
- **Class 0x48 – Quality of Service**  
  Default DSCP priorities (Urgent 55, Scheduled 47, High 43, Low 31, Explicit 27); attributes 1–3 remain read-only in this port.
- **Class 0x47 – Device Level Ring**  
  Present in the code base but **not** instantiated on this platform because the ESP32-P4 design has only a single Ethernet port and lacks the dual-MAC hardware required for ring supervision.

## I/O Assemblies
- `Input Assembly 100` (`g_assembly_data064`, 32 bytes): produced data for originators; contains VL53L1X sensor data at configurable byte offset (default: bytes 0-8, configurable to 9-17 or 18-26); sensor data includes distance, status, ambient, signal quality, and SPAD count; also contains MCP230XX input device data at configurable byte offsets; contains MPU6050 IMU data at configurable byte offset (default: bytes 18-31, uses 14 bytes); bytes outside configured ranges are available for other application data
- `Output Assembly 150` (`g_assembly_data096`, 32 bytes): consumed data written by originators; bit 0 controls GPIO33 status LED; contains MCP230XX output device data at configurable byte offsets; updates can trigger local actions
- `Configuration Assembly 151` (`g_assembly_data097`, 10 bytes): optional per-connection configuration image
- Exclusive Owner, Input Only, and Listen Only connection points are pre-configured for assembly 100/150/151 triplets
- Run/Idle headers for both O→T and T→O traffic are disabled by default (can be re-enabled if required)

## Network Configuration
- Defaults to DHCP when no persisted configuration is present or if the stored static profile fails validation
- **Web UI Configuration**: Network settings can be configured via the web interface (Configuration page → Network Configuration card)
- **CIP Configuration**: Also supports static addressing through CIP attribute writes:
  - Attribute 3 (`config_control`) selects DHCP (`0x02`) or static (`0x00`) mode
  - Attribute 5 (`interface_configuration`) carries the static IP, mask, gateway, and DNS values
- Hostname (attribute 6) and domain name storage comply with RFC 1123 length limits and input validation
- Encapsulation inactivity timeout (attribute 13) constrained to 0–3600 seconds per spec
- DNS servers propagated to `esp_netif` whenever non-zero in the CIP structure
- All settings persist in NVS (`namespace: opener`, key `tcpip_cfg`) via `NvTcpipStore()`/`NvTcpipLoad()`
- Invalid or partially populated static entries are rejected, clearing the interface back to DHCP and resetting unresolved ACD status bits
- **Note**: Network configuration changes require a device reboot to take effect

## Runtime Integration Notes
- Ethernet link events from `esp_event` update the Identity object’s state and clear/set recoverable fault flags
- GPIO33 is configured as a status LED drive and is toggled from the output assembly (bit 0 of assembly 150)
- Mutex-protected `struct netif*` handle allows the sample application and OpENer to share the active lwIP netif
- Encapsulation layer uses OpENer’s standard socket abstraction and ESP32 FreeRTOS tasks for TCP/UDP servicing

## Example Object Views
- Identity object instance 1 (`0x01/1`) as displayed in Molex EtherNet/IP Tools, showing the operational state and extended status `0x06`:  
  ![Identity object](images/identity.png)
- Ethernet Link object (`0xF6/1`) reporting 100 Mbps twisted-pair link, auto-negotiation results, interface counters, and MAC address `30:ED:A0:E0:FD:96`:  
  ![Ethernet Link object](images/EthernetLink.png)
- Quality of Service object (`0x48/1`) with default DSCP values; note that attributes 1–3 (802.1Q enable and PTP DSCP overrides) are read-only in this port:  
  ![Quality of Service object](images/QOS.png)
- TCP/IP Interface object (`0xF5/1`) reflecting the current network settings, stored configuration control, multicast allocation, and ACD configuration:  
  ![TCP/IP Interface object](images/TCP-IP.png)

## Address Conflict Detection (ACD)

- RFC 5227 compliant Address Conflict Detection (ACD) is implemented for static IP assignment
- ACD can be enabled/disabled via TCP/IP Interface Object Attribute #10 (`select_acd`)
- When enabled, static IP addresses are deferred until ACD confirms they are safe to use
- Configuration persists in NVS and survives reboots
- See [ReadmeACD.md](ReadmeACD.md) for detailed testing instructions
- Additional implementation notes: [dependency_modifications/lwIP/acd-static-ip-issue.md](dependency_modifications/lwIP/acd-static-ip-issue.md)

## Test Reports
- High-speed TON/TOF timing validation with Micro850 ladder logic and Saleae capture: see [Testing/Test1.md](Testing/Test1.md).

## Hardware Under Test
- All bring-up and validation have been performed on a Waveshare ESP32-P4-NANO with PoE module ([product page](https://www.waveshare.com/esp32-p4-nano.htm?sku=29028)).

## Next Steps
- Expand runtime configuration and diagnostics for newly integrated peripherals:
  - Enumerate connected sensors/expanders during boot and publish their status through EtherNet/IP assemblies.
  - Add automated self-test routines and logging for critical peripherals (IMUs, load-cell ADCs, ToF sensor).
  - Extend CIP object model to expose peripheral data streams (e.g., motion telemetry, weight measurements, distance sensing) for PLC consumption.
  - Integrate MPU6050 IMU data into EtherNet/IP assemblies and web interface:
    - Add IMU configuration page to web UI
    - Map accelerometer, gyroscope, and orientation data to Input Assembly 100
    - Provide real-time orientation visualization
    - Support multiple IMU devices on the same I2C bus



