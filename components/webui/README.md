# Web UI Component

A comprehensive web-based user interface for the ESP32-P4 OpENer EtherNet/IP adapter, providing real-time sensor monitoring, configuration management, and firmware updates.

## Overview

The Web UI component provides a modern, responsive web interface accessible via HTTP on port 80. It offers complete device configuration, real-time sensor data visualization, EtherNet/IP assembly monitoring, and over-the-air (OTA) firmware update capabilities.

## Features

- **Network Configuration**: Configure DHCP/Static IP, netmask, gateway, and DNS settings
- **MPU6050 Sensor Configuration**: Enable/disable sensor and view real-time readings
- **Real-time Sensor Monitoring**: Live orientation and pressure data
- **EtherNet/IP Assembly Monitoring**: Bit-level visualization of Input and Output assemblies
- **Modbus TCP Configuration**: Enable/disable Modbus TCP server
- **OTA Firmware Updates**: Upload and install firmware updates via web interface
- **Responsive Design**: Works on desktop and mobile devices
- **No External Dependencies**: All CSS and JavaScript is self-contained (no CDN)

## Web Pages

### Configuration Page (`/`)
The main configuration page provides access to all device settings:

- **Network Configuration Card**
  - DHCP/Static IP mode selection
  - IP address, netmask, gateway configuration
  - DNS server configuration (hidden when using DHCP)
  - All settings stored in OpENer's NVS

- **Modbus TCP Card**
  - Enable/disable Modbus TCP server
  - Information about Modbus data mapping
  - Settings persist across reboots

- **MPU6050 Sensor Configuration Card**
  - Enable/disable sensor
  - View sensor status and readings

### MPU6050 Status Page (`/mpu6050`)
Real-time sensor monitoring dashboard:

- **Sensor Readings**
  - Fused angle from vertical (degrees)
  - Cylinder 1 pressure (PSI)
  - Cylinder 2 pressure (PSI)
  - Temperature (Celsius)

- **Updates every 250ms**

### Input Assembly Page (`/inputassembly`)
Bit-level visualization of EtherNet/IP Input Assembly 100:

- 32 bytes displayed individually
- Each byte shows: `Byte X HEX (0xYY) | DEC ZZZ`
- Individual bit checkboxes (read-only)
- Blue checkboxes with white checkmarks when active
- Auto-refresh every 250ms

### Output Assembly Page (`/outputassembly`)
Bit-level visualization of EtherNet/IP Output Assembly 150:

- 32 bytes displayed individually
- Each byte shows: `Byte X HEX (0xYY) | DEC ZZZ`
- Individual bit checkboxes (read-only)
- Blue checkboxes with white checkmarks when active
- Auto-refresh every 250ms

### Firmware Update Page (`/ota`)
Over-the-air firmware update interface:

- File upload for firmware binary
- Progress indication
- Auto-redirect to home page after successful update
- Styled file input button matching application design

## REST API Endpoints

All API endpoints return JSON responses.

### Configuration Endpoints

#### `GET /api/sensor/status`
Get current MPU6050 sensor status and readings.

**Response:**
```json
{
  "enabled": true,
  "angle_deg": 12.34,
  "pressure1_psi": 123.4,
  "pressure2_psi": 876.6,
  "temperature_c": 25.5
}
```

### Status Endpoints

#### `GET /api/assemblies`
Get EtherNet/IP assembly data.

**Response:**
```json
{
  "input_assembly_100": {
    "raw_bytes": [0, 1, 2, ...]
  },
  "output_assembly_150": {
    "raw_bytes": [0, 0, 0, ...]
  }
}
```

### Network Configuration Endpoints

#### `GET /api/ipconfig`
Get current IP configuration.

**Response:**
```json
{
  "use_dhcp": true,
  "ip_address": "192.168.1.100",
  "netmask": "255.255.255.0",
  "gateway": "192.168.1.1",
  "dns1": "8.8.8.8",
  "dns2": "8.8.4.4"
}
```

#### `POST /api/ipconfig`
Update IP configuration.

**Request Body:**
```json
{
  "use_dhcp": false,
  "ip_address": "192.168.1.100",
  "netmask": "255.255.255.0",
  "gateway": "192.168.1.1",
  "dns1": "8.8.8.8",
  "dns2": "8.8.4.4"
}
```

**Response:**
```json
{
  "status": "ok",
  "message": "IP configuration saved successfully. Reboot required to apply changes."
}
```

### Modbus Configuration Endpoints

#### `GET /api/modbus`
Get Modbus TCP enabled state.

**Response:**
```json
{
  "enabled": true
}
```

#### `POST /api/modbus`
Set Modbus TCP enabled state.

**Request Body:**
```json
{
  "enabled": true
}
```

**Response:**
```json
{
  "status": "ok",
  "enabled": true,
  "message": "Modbus configuration saved successfully"
}
```

### Sensor Control Endpoints

#### `GET /api/sensor/enabled`
Get sensor enabled state.

**Response:**
```json
{
  "enabled": true
}
```

#### `POST /api/sensor/enabled`
Enable or disable the VL53L1x sensor.

**Request Body:**
```json
{
  "enabled": true
}
```

**Response:**
```json
{
  "status": "ok",
  "enabled": true,
  "message": "Sensor state saved successfully"
}
```


### OTA Endpoints

#### `POST /api/ota/update`
Trigger OTA firmware update.

**Request:** Multipart form data with firmware file, or JSON with URL:
```json
{
  "url": "http://example.com/firmware.bin"
}
```

**Response:**
```json
{
  "status": "ok",
  "message": "OTA update started"
}
```

#### `GET /api/ota/status`
Get OTA update status.

**Response:**
```json
{
  "status": "idle",
  "progress": 0
}
```

### System Endpoints

#### `POST /api/reboot`
Reboot the device.

**Response:**
```json
{
  "status": "ok",
  "message": "Device will reboot in 2 seconds"
}
```

## Architecture

### Components

- **`webui.c`**: HTTP server initialization and page routing
- **`webui_html.c`**: HTML, CSS, and JavaScript for all web pages (embedded as C strings)
- **`webui_api.c`**: REST API endpoint handlers

### HTTP Server Configuration

- **Port**: 80
- **Max URI Handlers**: 25
- **Max Open Sockets**: 7
- **Stack Size**: 16KB
- **Task Priority**: 5
- **Core**: 1 (runs on same core as sensor task)
- **Max Request Header Length**: 1024 bytes

### Data Storage

- **Network Configuration**: Stored in OpENer's `g_tcpip` NVS namespace
- **Modbus Configuration**: Stored in `system` NVS namespace
- **Sensor Enabled State**: Stored in `system` NVS namespace

### Sensor Data Mapping

The MPU6050 sensor data is written to Input Assembly 100 (`g_assembly_data064`) as 4 DINTs (16 bytes):

- **Bytes 0-3**: DINT 0 - Fused angle (degrees * 100, little-endian)
- **Bytes 4-7**: DINT 1 - Cylinder 1 pressure (PSI * 10, little-endian)
- **Bytes 8-11**: DINT 2 - Cylinder 2 pressure (PSI * 10, little-endian)
- **Bytes 12-15**: DINT 3 - Temperature (Celsius * 100, little-endian)

When the sensor is disabled, these bytes are zeroed out.

## Usage

### Accessing the Web Interface

1. Ensure the ESP32-P4 device is powered on and connected to your network
2. Find the device's IP address (check serial monitor or DHCP server)
3. Open a web browser and navigate to `http://<device-ip>`
4. The Configuration page will load automatically

### Configuration Workflow

1. **Network Setup** (if needed):
   - Navigate to Configuration page
   - Configure IP settings in Network Configuration card
   - Click "Save Network Configuration"
   - Reboot device to apply changes

2. **Sensor Configuration**:
   - Navigate to Configuration page
   - Enable/disable sensor in MPU6050 Sensor Configuration card
   - Changes take effect immediately

3. **Monitoring**:
   - Navigate to MPU6050 Status page for real-time readings
   - Navigate to Input/Output Assembly pages for bit-level data

### Firmware Update

1. Navigate to Firmware Update page (`/ota`)
2. Click "Choose File" and select firmware binary
3. Click "Start Update"
4. Wait for update to complete
5. Device will automatically reboot
6. Browser will redirect to home page

## Development

### Adding a New Page

1. Add HTML function in `webui_html.c`:
   ```c
   const char *webui_get_newpage_html(void)
   {
       return "<!DOCTYPE html>..."
   }
   ```

2. Register URI handler in `webui.c`:
   ```c
   httpd_uri_t newpage_uri = {
       .uri = "/newpage",
       .method = HTTP_GET,
       .handler = newpage_handler,
       .user_ctx = NULL
   };
   httpd_register_uri_handler(server_handle, &newpage_uri);
   ```

3. Implement handler function that calls the HTML getter

### Adding a New API Endpoint

1. Implement handler function in `webui_api.c`:
   ```c
   static esp_err_t api_get_newendpoint_handler(httpd_req_t *req)
   {
       // Implementation
   }
   ```

2. Register in `webui_register_api_handlers()`:
   ```c
   httpd_uri_t get_newendpoint_uri = {
       .uri = "/api/newendpoint",
       .method = HTTP_GET,
       .handler = api_get_newendpoint_handler,
       .user_ctx = NULL
   };
   httpd_register_uri_handler(server, &get_newendpoint_uri);
   ```

### Updating Preview Files

Preview HTML files in `webui_preview/` can be updated using the extraction script:

```bash
python update_webui_previews.py
```

This script extracts HTML from `webui_html.c` and updates all preview files.

## Notes

- All settings persist across reboots via NVS
- Network configuration changes require a reboot to take effect
- Sensor enable/disable changes take effect immediately
- The web UI has no external dependencies (no CDN, all assets embedded)
- The sensor navigation item is hidden when the sensor is disabled

## Footer

All pages display the footer:
```
OpENer Ethernet/IP for ESP32-P4 | Adam G Sweeney
```

