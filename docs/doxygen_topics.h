/**
 * @file doxygen_topics.h
 * @brief Doxygen topic pages for project components and APIs
 *
 * This file contains topic pages (@page) that appear in the Doxygen "Topics" section.
 * Each component and major feature has its own topic page for easy navigation.
 */

/**
 * @mainpage ESP32-P4 EtherNet/IP Device Documentation
 *
 * @section intro_sec Introduction
 *
 * This project implements a full-featured EtherNet/IP adapter device on the ESP32-P4 platform
 * using the OpENer open-source EtherNet/IP stack. The device serves as a bridge between
 * EtherNet/IP networks and local I/O, sensors, and other industrial automation components.
 *
 * @section features_sec Key Features
 *
 * - **EtherNet/IP Adapter**: Full OpENer stack implementation with I/O connections
 * - **IMU Integration**: 6-axis motion sensor support (MPU6050 primary, LSM6DS3 fallback)
 * - **Modbus TCP Server**: Standard Modbus TCP/IP server (port 502)
 * - **Web-Based Configuration Interface**: Essential device management via web UI
 * - **OTA Firmware Updates**: Over-the-air firmware update capability
 * - **RFC 5227 Compliant Network Configuration**: Address Conflict Detection (ACD)
 *
 * @section components_sec Components
 *
 * - @ref component_mpu6050 "MPU6050 IMU Driver"
 * - @ref component_lsm6ds3 "LSM6DS3 IMU Driver"
 * - @ref component_gp8403_dac "GP8403 DAC Driver"
 * - @ref component_current_loop "4-20mA Current Loop Driver"
 * - @ref component_modbus_tcp "Modbus TCP Server"
 * - @ref component_webui "Web UI Component"
 * - @ref component_web_api "Web API Documentation"
 * - @ref component_ota_manager "OTA Manager"
 * - @ref component_system_config "System Configuration"
 * - @ref component_log_buffer "Log Buffer"
 *
 * @section api_sec APIs
 *
 * - @ref component_web_api "Web REST API"
 * - @ref component_opener_api "OpENer EtherNet/IP API"
 *
 * @section hardware_sec Hardware Requirements
 *
 * - **Microcontroller**: ESP32-P4
 * - **Ethernet PHY**: IP101 (or compatible)
 * - **I2C Devices**: Optional IMU sensor (MPU6050 or LSM6DS3)
 *
 * @section software_sec Software Requirements
 *
 * - **ESP-IDF**: v5.5.1 or compatible
 * - **Python**: 3.x (for build scripts)
 * - **CMake**: 3.16 or higher
 */

/**
 * @page component_mpu6050 MPU6050 IMU Driver
 *
 * @section mpu6050_overview Overview
 *
 * The MPU6050 driver provides an interface to communicate with the MPU6050 6-axis accelerometer
 * and gyroscope sensor via I2C. It supports reading accelerometer, gyroscope, and temperature
 * data, as well as configuring sensor parameters.
 *
 * @section mpu6050_features Features
 *
 * - I2C communication interface
 * - Accelerometer and gyroscope data reading
 * - Temperature sensor reading
 * - Orientation calculation (roll, pitch, ground angle)
 * - Cylinder pressure calculations for opposed cylinders
 * - Configurable sensor parameters (sample rate, range, etc.)
 *
 * @section mpu6050_usage Usage
 *
 * @code{.c}
 * #include "mpu6050.h"
 *
 * mpu6050_t mpu;
 * mpu6050_init(&mpu, I2C_NUM_0, MPU6050_ADDR_AD0_LOW);
 *
 * mpu6050_sample_t sample;
 * if (mpu6050_read_sample(&mpu, &sample) == ESP_OK) {
 *     // Use sample data
 * }
 * @endcode
 *
 * @section mpu6050_api API Reference
 *
 * See @ref mpu6050.h for complete API documentation.
 *
 * - mpu6050_init() - Initialize the MPU6050 sensor
 * - mpu6050_read_sample() - Read accelerometer, gyroscope, and temperature data
 * - mpu6050_calculate_orientation() - Calculate roll, pitch, and ground angle
 * - mpu6050_set_accel_range() - Configure accelerometer range
 * - mpu6050_set_gyro_range() - Configure gyroscope range
 */

/**
 * @page component_lsm6ds3 LSM6DS3 IMU Driver
 *
 * @section lsm6ds3_overview Overview
 *
 * The LSM6DS3 driver provides an interface to communicate with the LSM6DS3 6-axis accelerometer
 * and gyroscope sensor via I2C or SPI. It supports reading accelerometer and gyroscope data,
 * configuring sensor parameters, and calibration.
 *
 * @section lsm6ds3_features Features
 *
 * - I2C and SPI communication interfaces
 * - Accelerometer and gyroscope data reading
 * - Sensor fusion (complementary filter) for accurate orientation
 * - Orientation calculation (roll, pitch, ground angle)
 * - Cylinder pressure calculations for opposed cylinders
 * - Calibration support
 * - Configurable sensor parameters
 *
 * @section lsm6ds3_usage Usage
 *
 * @code{.c}
 * #include "lsm6ds3.h"
 *
 * lsm6ds3_handle_t sensor;
 * lsm6ds3_config_t config = {
 *     .interface = LSM6DS3_INTERFACE_I2C,
 *     .i2c_port = I2C_NUM_0,
 *     .i2c_addr = LSM6DS3_I2C_ADDR_HIGH
 * };
 * lsm6ds3_init(&sensor, &config);
 *
 * lsm6ds3_sample_t sample;
 * if (lsm6ds3_read_sample(&sensor, &sample) == ESP_OK) {
 *     // Use sample data
 * }
 * @endcode
 *
 * @section lsm6ds3_api API Reference
 *
 * See @ref lsm6ds3.h for complete API documentation.
 *
 * - lsm6ds3_init() - Initialize the LSM6DS3 sensor
 * - lsm6ds3_read_sample() - Read accelerometer and gyroscope data
 * - lsm6ds3_calculate_orientation() - Calculate roll, pitch, and ground angle
 * - lsm6ds3_calibrate() - Calibrate the sensor
 */

/**
 * @page component_gp8403_dac GP8403 DAC Driver
 *
 * @section gp8403_overview Overview
 *
 * The GP8403 driver provides an interface to the DFRobot Gravity 2-Channel I2C DAC Module.
 * This module converts digital signals to analog voltage outputs (0-10V or 0-5V).
 *
 * @section gp8403_features Features
 *
 * - Dual-channel DAC output
 * - 12-bit resolution
 * - I2C communication interface
 * - Configurable output range (0-5V or 0-10V)
 * - Independent channel control
 *
 * @section gp8403_usage Usage
 *
 * @code{.c}
 * #include "gp8403_dac.h"
 *
 * gp8403_dac_t dac;
 * gp8403_dac_init(&dac, I2C_NUM_0, GP8403_DAC_ADDR);
 *
 * // Set channel 0 to 5.0V
 * gp8403_dac_set_voltage(&dac, 0, 5.0f);
 * @endcode
 *
 * @section gp8403_api API Reference
 *
 * See @ref gp8403_dac.h for complete API documentation.
 *
 * - gp8403_dac_init() - Initialize the GP8403 DAC
 * - gp8403_dac_set_voltage() - Set output voltage for a channel
 * - gp8403_dac_set_range() - Configure output voltage range
 */

/**
 * @page component_current_loop 4-20mA Current Loop Driver
 *
 * @section current_loop_overview Overview
 *
 * The 4-20mA Current Loop driver provides an interface for generating 4-20mA current loop
 * outputs, commonly used in industrial automation for sensor communication.
 *
 * @section current_loop_features Features
 *
 * - 4-20mA current loop output
 * - Configurable current levels
 * - Industrial standard compliance
 *
 * @section current_loop_api API Reference
 *
 * See @ref current_loop_4_20ma.h for complete API documentation.
 */

/**
 * @page component_modbus_tcp Modbus TCP Server
 *
 * @section modbus_overview Overview
 *
 * The Modbus TCP server component implements a standard Modbus TCP/IP server on port 502,
 * providing access to EtherNet/IP assembly data via Modbus registers.
 *
 * @section modbus_features Features
 *
 * - Standard Modbus TCP/IP protocol (port 502)
 * - Input Registers 0-15 map to Input Assembly 100
 * - Holding Registers 100-115 map to Output Assembly 150
 * - Thread-safe operation
 * - Multiple concurrent connections
 *
 * @section modbus_mapping Register Mapping
 *
 * - **Input Registers 0-15**: Maps to EtherNet/IP Input Assembly 100 (32 bytes)
 * - **Holding Registers 100-115**: Maps to EtherNet/IP Output Assembly 150 (32 bytes)
 *
 * @section modbus_api API Reference
 *
 * See @ref modbus_tcp.h for complete API documentation.
 *
 * - modbus_tcp_init() - Initialize the Modbus TCP server
 * - modbus_tcp_start() - Start the Modbus TCP server
 * - modbus_tcp_stop() - Stop the Modbus TCP server
 */

/**
 * @page component_webui Web UI Component
 *
 * @section webui_overview Overview
 *
 * The Web UI component provides a lightweight, responsive web interface accessible via HTTP
 * on port 80. It focuses on essential functions: network configuration and firmware updates.
 * All other device configuration, monitoring, and status information is available via the REST API.
 *
 * @section webui_features Features
 *
 * - **Network Configuration**: Configure DHCP/Static IP, netmask, gateway, and DNS settings
 * - **OTA Firmware Updates**: Upload and install firmware updates via web interface
 * - **REST API**: All sensor configuration, monitoring, and advanced features available via API endpoints
 * - **Responsive Design**: Works on desktop and mobile devices
 * - **No External Dependencies**: All CSS and JavaScript is self-contained (no CDN)
 *
 * @section webui_pages Web Pages
 *
 * ### Configuration Page (`/`)
 * The main configuration page provides essential device management:
 * - Network Configuration Card
 *   - DHCP/Static IP mode selection
 *   - IP address, netmask, gateway configuration
 *   - DNS server configuration (hidden when using DHCP)
 *   - All settings stored in OpENer's NVS
 *   - Reboot required to apply network changes
 *
 * ### Firmware Update Page (`/ota`)
 * Over-the-air firmware update interface:
 * - File upload for firmware binary
 * - Progress indication
 * - Auto-redirect to home page after successful update
 *
 * @section webui_api API Reference
 *
 * See @ref webui_api.h for complete API documentation.
 *
 * - webui_register_api_handlers() - Register all API endpoint handlers
 * - webui_get_index_html() - Get index HTML page
 * - webui_get_status_html() - Get status HTML page
 * - webui_get_ota_html() - Get OTA HTML page
 */

/**
 * @page component_web_api Web REST API
 *
 * @section webapi_overview Overview
 *
 * The Web REST API provides comprehensive access to device configuration, sensor data,
 * and system status via HTTP endpoints. All endpoints return JSON responses and are
 * accessible at `http://<device-ip>/api`.
 *
 * @section webapi_base Base URL
 *
 * All API endpoints are prefixed with `/api`. The base URL is typically `http://<device-ip>/api`.
 *
 * @section webapi_response Response Format
 *
 * All endpoints return JSON responses. Success responses typically include:
 * - `status`: "ok" or "error"
 * - `message`: Human-readable message
 * - Additional endpoint-specific fields
 *
 * Error responses include:
 * - `status`: "error"
 * - `message`: Error description
 *
 * HTTP status codes:
 * - `200 OK`: Success
 * - `400 Bad Request`: Invalid request parameters
 * - `500 Internal Server Error`: Server-side error
 * - `503 Service Unavailable`: Service not available
 *
 * @section webapi_endpoints Endpoints
 *
 * ### System Configuration
 *
 * - `GET /api/ipconfig` - Get current IP network configuration
 * - `POST /api/ipconfig` - Set IP network configuration (reboot required)
 * - `POST /api/reboot` - Reboot the device
 * - `GET /api/logs` - Get system logs from the log buffer
 *
 * ### MPU6050 Sensor
 *
 * - `GET /api/mpu6050/status` - Get MPU6050 sensor status and readings
 * - `GET /api/mpu6050/enabled` - Get MPU6050 enabled state
 * - `POST /api/mpu6050/enabled` - Set MPU6050 enabled state
 * - `GET /api/mpu6050/config` - Get MPU6050 configuration
 * - `POST /api/mpu6050/config` - Set MPU6050 configuration
 *
 * ### LSM6DS3 Sensor
 *
 * - `GET /api/lsm6ds3/status` - Get LSM6DS3 sensor status and readings
 * - `GET /api/lsm6ds3/enabled` - Get LSM6DS3 enabled state
 * - `POST /api/lsm6ds3/enabled` - Set LSM6DS3 enabled state
 * - `GET /api/lsm6ds3/config` - Get LSM6DS3 configuration
 * - `POST /api/lsm6ds3/config` - Set LSM6DS3 configuration
 * - `POST /api/lsm6ds3/calibrate` - Start LSM6DS3 calibration
 *
 * ### EtherNet/IP Assemblies
 *
 * - `GET /api/assemblies` - Get EtherNet/IP assembly data
 * - `GET /api/assemblies/input` - Get Input Assembly 100 data
 * - `GET /api/assemblies/output` - Get Output Assembly 150 data
 *
 * ### Modbus TCP
 *
 * - `GET /api/modbus/status` - Get Modbus TCP server status
 * - `POST /api/modbus/enabled` - Enable/disable Modbus TCP server
 *
 * @section webapi_docs Complete Documentation
 *
 * For complete API documentation with request/response examples, see:
 * - @ref docs/API_Endpoints.md "API Endpoints Documentation"
 */

/**
 * @page component_ota_manager OTA Manager
 *
 * @section ota_overview Overview
 *
 * The OTA Manager component provides over-the-air firmware update capability for the ESP32-P4 device.
 * It supports both file upload via web interface and URL-based downloads.
 *
 * @section ota_features Features
 *
 * - File upload via web interface
 * - URL-based firmware downloads
 * - Automatic rollback on failure
 * - Progress tracking
 * - Partition management
 *
 * @section ota_usage Usage
 *
 * @code{.c}
 * #include "ota_manager.h"
 *
 * ota_config_t config = {
 *     .url = "http://example.com/firmware.bin",
 *     .timeout_ms = 30000
 * };
 *
 * esp_err_t err = ota_manager_start_update(&config);
 * @endcode
 *
 * @section ota_api API Reference
 *
 * See @ref ota_manager.h for complete API documentation.
 *
 * - ota_manager_init() - Initialize the OTA manager
 * - ota_manager_start_update() - Start an OTA update
 * - ota_manager_get_status() - Get current OTA status
 */

/**
 * @page component_system_config System Configuration
 *
 * @section sysconfig_overview Overview
 *
 * The System Configuration component provides centralized configuration management for
 * system-level settings including I2C configuration and MCP (Microchip I/O expander) settings.
 *
 * @section sysconfig_features Features
 *
 * - I2C bus configuration
 * - MCP I/O expander configuration
 * - NVS (Non-Volatile Storage) integration
 * - Persistent configuration storage
 *
 * @section sysconfig_api API Reference
 *
 * See @ref system_config.h, @ref i2c_config.h, and @ref mcp_config.h for complete API documentation.
 */

/**
 * @page component_log_buffer Log Buffer
 *
 * @section logbuffer_overview Overview
 *
 * The Log Buffer component provides a circular buffer for storing system logs, allowing
 * retrieval of recent log entries via the web API.
 *
 * @section logbuffer_features Features
 *
 * - Circular buffer implementation
 * - Configurable buffer size
 * - Thread-safe operation
 * - Web API integration for log retrieval
 *
 * @section logbuffer_api API Reference
 *
 * See @ref log_buffer.h for complete API documentation.
 *
 * - log_buffer_init() - Initialize the log buffer
 * - log_buffer_write() - Write a log entry
 * - log_buffer_read() - Read log entries
 */

/**
 * @page component_opener_api OpENer EtherNet/IP API
 *
 * @section opener_overview Overview
 *
 * OpENer is an open-source EtherNet/IP communication stack for adapter devices (connection target).
 * It supports multiple I/O and explicit connections and includes features required by the CIP
 * specification to enable devices to comply with ODVA's conformance/interoperability tests.
 *
 * @section opener_features Features
 *
 * - EtherNet/IP adapter implementation
 * - Multiple I/O connections support
 * - Explicit messaging support
 * - CIP object support
 * - Assembly objects for I/O data
 *
 * @section opener_api API Reference
 *
 * See @ref opener_api.h for complete API documentation.
 *
 * The OpENer API includes:
 * - Stack initialization and configuration
 * - CIP object creation and management
 * - Assembly object management
 * - Connection management
 * - Callback functions for platform-specific operations
 *
 * @section opener_docs Documentation
 *
 * For detailed OpENer documentation, see the OpENer main page in the generated documentation.
 */

