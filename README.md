# ESP32-P4 OpENer EtherNet/IP Adapter

A comprehensive EtherNet/IP communication adapter for the ESP32-P4 microcontroller, integrating sensor support, Modbus TCP, and web-based configuration.

## Overview

This project implements a full-featured EtherNet/IP adapter device on the ESP32-P4 platform using the OpENer open-source EtherNet/IP stack. The device serves as a bridge between EtherNet/IP networks and local I/O, sensors, and other industrial automation components.

### Key Features

- **EtherNet/IP Adapter**: Full OpENer stack implementation with I/O connections
  - Input Assembly 100 (32 bytes)
  - Output Assembly 150 (32 bytes)
  - Configuration Assembly 151 (10 bytes)
  - Support for Exclusive Owner, Input Only, and Listen Only connections

- **MPU6050 IMU Integration**: 6-axis motion sensor support
  - Roll and pitch angle calculations
  - Ground angle (absolute tilt) measurement
  - Sensor fusion for accurate orientation data
  - Configurable byte offset in Input Assembly
  - Calculates cylinder pressure requirements based on tool weight and angle

- **Modbus TCP Server**: Standard Modbus TCP/IP server (port 502)
  - Input Registers 0-15 map to Input Assembly 100
  - Holding Registers 100-115 map to Output Assembly 150

- **Web-Based Configuration Interface**: Comprehensive HTTP-based management
  - Network configuration (DHCP/Static IP)
  - Sensor configuration and calibration
  - Real-time assembly data monitoring
  - Firmware updates via OTA
  - System logs viewing

- **OTA Firmware Updates**: Over-the-air firmware update capability
  - File upload via web interface
  - URL-based downloads
  - Automatic rollback on failure


- **RFC 5227 Compliant Network Configuration**: Address Conflict Detection (ACD)
  - RFC 5227 compliant static IP assignment
  - Configurable ACD timing parameters
  - Custom lwIP modifications for EtherNet/IP requirements

## Hardware Requirements

- **Microcontroller**: ESP32-P4
- **Ethernet PHY**: IP101 (or compatible)
- **I2C Devices**: Optional MPU6050 IMU sensor
- **GPIO Configuration**:
  - Ethernet: MDC, MDIO, PHY Reset (configurable)
  - I2C: SDA, SCL (configurable, defaults GPIO7/GPIO8)

## Software Requirements

- **ESP-IDF**: v5.5.1 or compatible
- **Python**: 3.x (for build scripts)
- **CMake**: 3.16 or higher

## Project Structure

```
EIP_GROUND_TRUTH/
├── main/                    # Main application code
├── components/              # Custom components
│   ├── opener/             # OpENer EtherNet/IP stack
│   ├── mpu6050/            # MPU6050 IMU driver
│   ├── modbus_tcp/         # Modbus TCP server
│   ├── webui/              # Web interface
│   ├── ota_manager/        # OTA update manager
│   ├── system_config/      # System configuration
│   └── log_buffer/         # Log buffer component
├── eds/                     # EtherNet/IP EDS file
├── docs/                    # Documentation
├── dependency_modifications/ # lwIP modifications
├── scripts/                 # Build scripts
└── FirmwareImages/          # Compiled firmware binaries
```

## Building the Project

1. **Install ESP-IDF v5.5.1**:
   ```bash
   # Follow ESP-IDF installation guide
   # https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/
   ```

2. **Clone the repository**:
   ```bash
   git clone <repository-url>
   cd EIP_GROUND_TRUTH
   ```

3. **Configure the project**:
   ```bash
   idf.py menuconfig
   ```
   
   Key configuration options:
   - **Ethernet**: PHY address, GPIO pins (MDC, MDIO, Reset)
   - **I2C**: SDA/SCL GPIO pins, pull-up configuration
   - **ACD Timing**: RFC 5227 timing parameters

4. **Build the firmware**:
   ```bash
   idf.py build
   ```

5. **Flash the firmware**:
   ```bash
   idf.py flash
   ```

6. **Monitor serial output**:
   ```bash
   idf.py monitor
   ```

## Configuration

### Network Configuration

The device supports both DHCP and static IP configuration:

- **DHCP Mode**: Automatic IP assignment via DHCP server
- **Static IP Mode**: Manual configuration with Address Conflict Detection (ACD)
  - IP address, netmask, gateway
  - Primary and secondary DNS servers
  - Hostname configuration

Configuration can be done via:
- Web interface: `http://<device-ip>/`
- EtherNet/IP CIP services
- NVS (Non-Volatile Storage)

### Sensor Configuration

#### MPU6050 IMU

The MPU6050 provides orientation data:
- **Roll**: Rotation around X-axis (degrees)
- **Pitch**: Rotation around Y-axis (degrees)
- **Ground Angle**: Absolute tilt from vertical (degrees)
- **Pressure Calculations**: Bottom and top cylinder pressures based on tool weight and angle

Configuration via web API:
- Enable/disable sensor
- Set byte offset in Input Assembly 100 (0-20, uses 20 bytes)
- View real-time readings


### EtherNet/IP Configuration

The device exposes three assembly instances:

- **Assembly 100 (Input)**: 32 bytes of input data
  - MPU6050 sensor data (configurable offset, 20 bytes)
  - Available space for other sensor data

- **Assembly 150 (Output)**: 32 bytes of output data
  - Tool weight (byte 30)
  - Tip force (byte 31)
  - Control commands and other output data (bytes 0-29)

- **Assembly 151 (Configuration)**: 10 bytes for configuration

Connection types supported:
- **Exclusive Owner**: Bidirectional I/O connection
- **Input Only**: Unidirectional input connection
- **Listen Only**: Unidirectional input connection (multicast)

**For detailed byte-by-byte assembly data layout, see [docs/ASSEMBLY_DATA_LAYOUT.md](docs/ASSEMBLY_DATA_LAYOUT.md).**

## Web Interface

Access the web interface at `http://<device-ip>/` after the device has obtained an IP address.

### Features

- **Network Configuration**: Set IP address, netmask, gateway, DNS
- **Sensor Status**: Real-time MPU6050 readings and configuration
- **Assembly Monitoring**: View Input/Output assembly data with bit-level visualization
- **Modbus TCP**: Enable/disable Modbus TCP server
- **OTA Updates**: Upload firmware updates via web browser
- **System Logs**: View recent system logs (32KB buffer)
- **I2C Bus Scan**: View detected I2C devices

For detailed API documentation, see [docs/API_Endpoints.md](docs/API_Endpoints.md).

## Modbus TCP Mapping

The device provides a Modbus TCP server on port 502:

- **Input Registers** (0x04 function code):
  - 0-15: Maps to Input Assembly 100 (32 bytes = 16 registers)

- **Holding Registers** (0x03, 0x06, 0x10 function codes):
  - 100-115: Maps to Output Assembly 150 (32 bytes = 16 registers)
  - 150-154: Maps to Configuration Assembly 151 (10 bytes = 5 registers)

All assembly data is stored in little-endian format (Modbus converts to big-endian for transmission).

**For detailed register-to-byte mapping, see [docs/ASSEMBLY_DATA_LAYOUT.md](docs/ASSEMBLY_DATA_LAYOUT.md).**

## EtherNet/IP EDS File

The EDS (Electronic Data Sheet) file is located at `eds/ESP32P4_OPENER.eds`. This file can be imported into EtherNet/IP configuration tools to discover and configure the device.

Key device information:
- **Vendor**: AGSweeney Automation (Vendor Code: 55512)
- **Product**: ESP32P4-EIP (Product Code: 1)
- **Type**: General Purpose Discrete I/O

## Custom lwIP Modifications

This project includes custom modifications to the lwIP network stack for RFC 5227 compliance and EtherNet/IP optimization. See [dependency_modifications/LWIP_MODIFICATIONS.md](dependency_modifications/LWIP_MODIFICATIONS.md) for details.

### Key Modifications

- RFC 5227 compliant static IP assignment
- Configurable ACD timing parameters
- Increased socket and connection limits
- IRAM optimization for network performance
- Task affinity configuration

**Note**: These modifications are required for proper EtherNet/IP operation. They must be reapplied when upgrading ESP-IDF.

## OTA Firmware Updates

Firmware updates can be performed via:

1. **Web Interface**: Upload binary file directly
2. **REST API**: POST to `/api/ota/update` with file or URL
3. **EtherNet/IP**: Via CIP services (if implemented)

The device supports automatic rollback on failed updates and maintains two OTA partitions for safe updates.

## Logging and Debugging

- **Serial Logging**: Available via UART (default 115200 baud)
- **Web Log Buffer**: 32KB circular buffer accessible via web interface
- **Log Levels**: Configurable via menuconfig

## Contributing

This project uses custom components and modified dependencies. When contributing:

1. Follow ESP-IDF coding conventions
2. Document any lwIP modifications clearly
3. Test with real EtherNet/IP controllers
4. Update EDS file if assembly structure changes

## License

This project uses the OpENer EtherNet/IP stack, which is licensed under an adapted BSD-style license. See the OpENer license file for details.

**Note**: EtherNet/IP™ is a trademark of ODVA, Inc.

## Troubleshooting

### Device Not Visible on Network

1. Check Ethernet cable connection
2. Verify PHY address configuration (default: 1)
3. Check serial logs for link status
4. Verify GPIO pin configuration

### EtherNet/IP Connection Fails

1. Verify IP address is not in conflict (check ACD status)
2. Check firewall rules (port 44818/TCP and UDP)
3. Verify EDS file is imported correctly
4. Check connection timeout settings

### Sensor Not Detected

1. Run I2C bus scan via web interface
2. Verify I2C address (MPU6050: 0x68 or 0x69)
3. Check pull-up resistors (internal vs external)
4. Review serial logs for I2C errors

### OTA Update Fails

1. Verify partition table has OTA partitions
2. Check available flash space
3. Verify firmware binary is for correct ESP32-P4 variant
4. Check serial logs for OTA error messages

## References

- [OpENer Documentation](https://github.com/EIPStackGroup/OpENer)
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/)
- [EtherNet/IP Specification](https://www.odva.org/)
- [Modbus TCP/IP Specification](https://modbus.org/specs.php)
- [RFC 5227 - IPv4 Address Conflict Detection](https://tools.ietf.org/html/rfc5227)

## Support

For issues and questions:
- Check the documentation in `docs/`
- Review serial logs for error messages
- Consult OpENer and ESP-IDF documentation

---

**Device Name**: ESP32P4-EIP  
**Vendor**: AG Sweeney 

**Firmware Version**: See git commit or build timestamp

