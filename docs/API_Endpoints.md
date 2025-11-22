# Web UI API Endpoints Documentation

This document describes all REST API endpoints available in the ESP32-P4 EtherNet/IP adapter web interface.

## Base URL

All API endpoints are prefixed with `/api`. The base URL is typically `http://<device-ip>/api`.

## Response Format

All endpoints return JSON responses. Success responses typically include:
- `status`: "ok" or "error"
- `message`: Human-readable message
- Additional endpoint-specific fields

Error responses include:
- `status`: "error"
- `message`: Error description

HTTP status codes:
- `200 OK`: Success
- `400 Bad Request`: Invalid request parameters
- `500 Internal Server Error`: Server-side error
- `503 Service Unavailable`: Service not available (e.g., log buffer disabled)

---

## System Configuration

### GET /api/ipconfig

Get current IP network configuration.

**Response:**
```json
{
  "use_dhcp": true,
  "ip_address": "172.16.82.99",
  "netmask": "255.255.255.0",
  "gateway": "172.16.82.1",
  "dns1": "8.8.8.8",
  "dns2": "8.8.4.4"
}
```

### POST /api/ipconfig

Set IP network configuration. **Reboot required** for changes to take effect.

**Request:**
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

**Notes:**
- If `use_dhcp` is `true`, static IP fields are ignored
- DNS fields are optional and can be set independently

---

### GET /api/reboot

Reboot the device.

**Request:** POST (no body required)

**Response:**
```json
{
  "status": "ok",
  "message": "Device rebooting..."
}
```

**Note:** The device will reboot immediately after sending the response.

---

### GET /api/logs

Get system logs from the log buffer.

**Response:**
```json
{
  "status": "ok",
  "logs": "I (12345) main: System started...\n...",
  "size": 1024,
  "total_size": 8192,
  "truncated": false
}
```

**Fields:**
- `logs`: String containing log entries (may be truncated to 32KB)
- `size`: Number of bytes returned
- `total_size`: Total size of log buffer
- `truncated`: Whether the response was truncated

**Note:** Returns 503 if log buffer is not enabled.

---

## VL53L1x Sensor Configuration

### GET /api/config

Get VL53L1x sensor configuration.

**Response:**
```json
{
  "distance_mode": 2,
  "timing_budget_ms": 50,
  "inter_measurement_ms": 50,
  "roi_x_size": 16,
  "roi_y_size": 16,
  "roi_center_spad": 199,
  "offset_mm": 0,
  "xtalk_cps": 0,
  "signal_threshold_kcps": 1024,
  "sigma_threshold_mm": 15,
  "threshold_low_mm": 0,
  "threshold_high_mm": 1000,
  "threshold_window": 0,
  "interrupt_polarity": 0,
  "i2c_address": 41
}
```

### POST /api/config

Update VL53L1x sensor configuration. All fields are optional.

**Request:**
```json
{
  "distance_mode": 2,
  "timing_budget_ms": 50,
  "inter_measurement_ms": 50,
  "roi_x_size": 16,
  "roi_y_size": 16,
  "roi_center_spad": 199,
  "offset_mm": 0,
  "xtalk_cps": 0,
  "signal_threshold_kcps": 1024,
  "sigma_threshold_mm": 15,
  "threshold_low_mm": 0,
  "threshold_high_mm": 1000,
  "threshold_window": 0,
  "interrupt_polarity": 0,
  "i2c_address": 41
}
```

**Response:**
```json
{
  "status": "ok",
  "message": "Configuration saved successfully"
}
```

**Notes:**
- Configuration is validated before saving
- Changes are applied immediately if the sensor is initialized
- Configuration is persisted to NVS

---

### GET /api/status

Get VL53L1x sensor status and current readings, plus EtherNet/IP assembly data.

**Response:**
```json
{
  "distance_mm": 1234,
  "status": 0,
  "ambient_kcps": 1376,
  "sig_per_spad_kcps": 3584,
  "num_spads": 256,
  "distance_mode": 2,
  "input_assembly_100": {
    "raw_bytes": [0, 1, 2, ...]
  },
  "output_assembly_150": {
    "led": false,
    "raw_bytes": [0, 0, 0, ...]
  }
}
```

**Fields:**
- `distance_mm`: Distance measurement in millimeters
- `status`: Sensor status code (0 = success)
- `ambient_kcps`: Ambient light level in kcps
- `sig_per_spad_kcps`: Signal per SPAD in kcps
- `num_spads`: Number of SPADs used
- `distance_mode`: Current distance mode (1=SHORT, 2=LONG)
- `input_assembly_100`: Input Assembly 100 data (32 bytes)
- `output_assembly_150`: Output Assembly 150 data (32 bytes)

---

### GET /api/assemblies

Get EtherNet/IP assembly data (simplified view).

**Response:**
```json
{
  "input_assembly_100": {
    "distance_mm": 1234,
    "status": 0,
    "ambient_kcps": 1376,
    "sig_per_spad_kcps": 3584,
    "num_spads": 256
  },
  "output_assembly_150": {
    "led": false
  },
  "config_assembly_151": {
    "size": 10
  }
}
```

---

### GET /api/assemblies/sizes

Get assembly buffer sizes.

**Response:**
```json
{
  "input_assembly_size": 32,
  "output_assembly_size": 32
}
```

---

### POST /api/calibrate/offset

Trigger offset calibration for VL53L1x sensor.

**Request:**
```json
{
  "target_distance_mm": 100
}
```

**Response:**
```json
{
  "status": "ok",
  "offset_mm": -5,
  "message": "Offset calibration successful"
}
```

**Notes:**
- Requires sensor to be initialized
- Calibration stops ranging temporarily
- Offset is clamped to -128 to +127 mm range
- Configuration is automatically saved and reapplied

---

### POST /api/calibrate/xtalk

Trigger crosstalk (xtalk) calibration for VL53L1x sensor.

**Request:**
```json
{
  "target_distance_mm": 100
}
```

**Response:**
```json
{
  "status": "ok",
  "xtalk_cps": 1024,
  "message": "Xtalk calibration successful"
}
```

**Notes:**
- Requires sensor to be initialized
- Calibration stops ranging temporarily
- Configuration is automatically saved and reapplied

---

### GET /api/sensor/enabled

Get VL53L1x sensor enabled state.

**Response:**
```json
{
  "enabled": true
}
```

### POST /api/sensor/enabled

Set VL53L1x sensor enabled state. Changes take effect immediately.

**Request:**
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

---

### GET /api/sensor/byteoffset

Get VL53L1x sensor data byte offset in Input Assembly 100.

**Response:**
```json
{
  "start_byte": 0,
  "end_byte": 8,
  "range": "0-8"
}
```

**Notes:**
- Valid ranges: 0-8, 9-17, or 18-26
- Sensor data uses 9 bytes

### POST /api/sensor/byteoffset

Set VL53L1x sensor data byte offset. Changes take effect immediately.

**Request:**
```json
{
  "start_byte": 0
}
```

**Response:**
```json
{
  "status": "ok",
  "start_byte": 0,
  "end_byte": 8,
  "range": "0-8",
  "message": "Sensor byte offset saved successfully"
}
```

**Valid values:** `0`, `9`, or `18`

---

## MPU6050 IMU Configuration

### GET /api/mpu6050/enabled

Get MPU6050 IMU enabled state.

**Response:**
```json
{
  "enabled": true
}
```

### POST /api/mpu6050/enabled

Set MPU6050 IMU enabled state.

**Request:**
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
  "message": "MPU6050 state saved successfully"
}
```

---

### GET /api/mpu6050/byteoffset

Get MPU6050 data byte offset in Input Assembly 100.

**Response:**
```json
{
  "start_byte": 0,
  "end_byte": 11,
  "range": "0-11"
}
```

**Notes:**
- MPU6050 data uses 12 bytes (3 int32_t: roll, pitch, ground_angle as scaled integers)
- Values are stored as `degrees * 10000` (e.g., 2.51 degrees = 25100)
- Valid start_byte: 0-20

### POST /api/mpu6050/byteoffset

Set MPU6050 data byte offset.

**Request:**
```json
{
  "start_byte": 0
}
```

**Response:**
```json
{
  "status": "ok",
  "start_byte": 0,
  "end_byte": 11,
  "message": "MPU6050 byte offset saved successfully"
}
```

**Valid values:** `0` to `20` (must fit 12 bytes within 32-byte assembly)

---

### GET /api/mpu6050/status

Get MPU6050 IMU sensor status and current readings.

**Response:**
```json
{
  "roll": 12.34,
  "pitch": -5.67,
  "ground_angle": 13.45,
  "roll_scaled": 123400,
  "pitch_scaled": -56700,
  "ground_angle_scaled": 134500,
  "enabled": true,
  "byte_offset": 18,
  "byte_range_start": 18,
  "byte_range_end": 29
}
```

**Fields:**
- `roll`: Roll angle in degrees (rotation around X-axis) as float
- `pitch`: Pitch angle in degrees (rotation around Y-axis) as float
- `ground_angle`: Absolute ground angle in degrees (total tilt angle relative to horizontal plane) as float
- `roll_scaled`: Roll angle as scaled integer (degrees * 10000, e.g., 2.51° = 25100)
- `pitch_scaled`: Pitch angle as scaled integer (degrees * 10000)
- `ground_angle_scaled`: Ground angle as scaled integer (degrees * 10000)
- `enabled`: Whether MPU6050 is enabled
- `byte_offset`: Starting byte offset in Input Assembly 100
- `byte_range_start`: Starting byte of MPU6050 data range
- `byte_range_end`: Ending byte of MPU6050 data range

**Notes:**
- Data is read from Input Assembly 100 at the configured byte offset
- MPU6050 uses 12 bytes: 3 int32_t values (roll, pitch, ground_angle) stored as scaled integers
- Values are stored as `degrees * 10000` (e.g., 2.51 degrees = 25100)
- Values are calculated from accelerometer data using orientation algorithms
- Roll and pitch are in degrees (typically -180 to +180 range)
- Ground angle is calculated as `sqrt(roll² + pitch²)` and represents total tilt regardless of direction
- The scaled integer values are what's actually stored in the Input Assembly (little-endian int32_t format)

---

## MCP230XX I/O Expander Configuration

### GET /api/mcp/enabled

Get MCP230XX I/O expander enabled state.

**Response:**
```json
{
  "enabled": true
}
```

### POST /api/mcp/enabled

Set MCP230XX I/O expander enabled state. **Restart required** for changes to take effect.

**Request:**
```json
{
  "enabled": true
}
```

**Response:**
```json
{
  "status": "ok",
  "message": "MCP enabled state saved successfully. Restart required for changes to take effect."
}
```

---

### GET /api/mcp/devicetype

Get MCP device type preference.

**Response:**
```json
{
  "device_type": 0,
  "device_name": "MCP23017"
}
```

**Device types:**
- `0`: MCP23017 (16-bit I/O expander)
- `1`: MCP23008 (8-bit I/O expander)

### POST /api/mcp/devicetype

Set MCP device type preference. **Restart required** for changes to take effect.

**Request:**
```json
{
  "device_type": 0
}
```

**Response:**
```json
{
  "status": "ok",
  "device_type": 0,
  "device_name": "MCP23017",
  "message": "MCP device type saved successfully. Restart required for changes to take effect."
}
```

---

### GET /api/mcp/updaterate

Get MCP I/O task update rate.

**Response:**
```json
{
  "update_rate_ms": 20,
  "update_rate_hz": 50.0
}
```

### POST /api/mcp/updaterate

Set MCP I/O task update rate. **Restart required** for changes to take effect.

**Request:**
```json
{
  "update_rate_ms": 20
}
```

**Response:**
```json
{
  "status": "ok",
  "update_rate_ms": 20,
  "update_rate_hz": 50.0,
  "message": "MCP update rate saved successfully. Restart required for changes to take effect."
}
```

**Valid range:** 10-1000 ms

---

### GET /api/mcp/devices

Get list of detected MCP devices with their configurations.

**Response:**
```json
{
  "devices": [
    {
      "i2c_address": 32,
      "detected": true,
      "device_type": 0,
      "device_name": "MCP23017",
      "enabled": true,
      "pin_directions": 65535,
      "input_byte_start": 0,
      "output_byte_start": 0,
      "output_logic_inverted": false
    }
  ],
  "count": 1
}
```

**Fields:**
- `i2c_address`: I2C address (0x20-0x27)
- `detected`: Whether device was detected at boot
- `device_type`: 0=MCP23017, 1=MCP23008
- `enabled`: Whether device is enabled
- `pin_directions`: Bitmask (0=output, 1=input)
  - MCP23017: 16-bit mask (0x0000-0xFFFF)
  - MCP23008: 8-bit mask (0x00-0xFF)
- `input_byte_start`: Starting byte in Input Assembly 100
- `output_byte_start`: Starting byte in Output Assembly 150
- `output_logic_inverted`: Whether output logic is inverted

---

### POST /api/mcp/device

Save configuration for a specific MCP device. **Restart required** for changes to take effect.

**Request:**
```json
{
  "i2c_address": 32,
  "device_type": 0,
  "enabled": true,
  "pin_directions": 65535,
  "input_byte_start": 0,
  "output_byte_start": 0,
  "output_logic_inverted": false
}
```

**Response:**
```json
{
  "status": "ok",
  "message": "Device configuration saved successfully. Restart required for changes to take effect."
}
```

**Validation:**
- `i2c_address`: Must be 0x20-0x27
- `device_type`: Must be 0 or 1
- `input_byte_start` + device_size must not exceed 32 bytes
- `output_byte_start` + device_size must not exceed 32 bytes
- Byte ranges must not overlap with other enabled devices
- MCP23017 uses 2 bytes, MCP23008 uses 1 byte

**Notes:**
- `pin_directions`: 0x0000/0x00 = all outputs, 0xFFFF/0xFF = all inputs
- Overlap checking considers all devices (enabled or disabled) that are detected

---

**Valid range:** 0-30 (must fit 2 bytes within 32-byte assembly)

---

## Modbus TCP Configuration

### GET /api/modbus

Get Modbus TCP server enabled state.

**Response:**
```json
{
  "enabled": true
}
```

### POST /api/modbus

Set Modbus TCP server enabled state. Changes take effect immediately.

**Request:**
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
  "message": "Modbus state saved successfully"
}
```

**Notes:**
- Modbus TCP server runs on port 502
- Input Registers 0-15 map to Input Assembly 100
- Holding Registers 100-115 map to Output Assembly 150

---

## I2C Bus Configuration

### GET /api/i2c/pullup

Get I2C internal pull-up enabled state.

**Response:**
```json
{
  "enabled": true
}
```

### POST /api/i2c/pullup

Set I2C internal pull-up enabled state. **Restart required** for changes to take effect.

**Request:**
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
  "message": "I2C pull-up setting saved. Restart required for changes to take effect."
}
```

**Notes:**
- Enables ESP32 internal pull-ups (~45kΩ) for I2C SDA/SCL lines
- Disable if using external pull-ups
- System-wide setting affects all I2C devices

---

## OTA (Over-The-Air) Firmware Update

### POST /api/ota/update

Trigger OTA firmware update. Supports two methods:

#### Method 1: File Upload (multipart/form-data)

Upload firmware binary file directly.

**Request:**
- Method: POST
- Content-Type: `multipart/form-data`
- Body: Multipart form with firmware file

**Response:**
```json
{
  "status": "ok",
  "message": "Firmware uploaded successfully. Finishing update and rebooting..."
}
```

**Notes:**
- Maximum file size: 2MB
- Device will reboot after successful upload
- Uses streaming to handle large files efficiently

#### Method 2: URL-based Update (application/json)

Download firmware from URL.

**Request:**
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

**Notes:**
- Update runs in background
- Check status with `/api/ota/status`

---

### GET /api/ota/status

Get OTA update status.

**Response:**
```json
{
  "status": "idle",
  "progress": 0,
  "message": "No update in progress"
}
```

**Status values:**
- `idle`: No update in progress
- `in_progress`: Update currently running
- `complete`: Update completed successfully
- `error`: Update failed

**Fields:**
- `progress`: Progress percentage (0-100)
- `message`: Status message

---

## Error Responses

All endpoints may return error responses in the following format:

```json
{
  "status": "error",
  "message": "Error description"
}
```

Common HTTP status codes:
- `400 Bad Request`: Invalid request parameters or JSON
- `500 Internal Server Error`: Server-side error (e.g., NVS save failed)
- `503 Service Unavailable`: Service not available (e.g., log buffer disabled)

---

## Notes

1. **Restart Required**: Some endpoints require a device restart for changes to take effect. These are clearly marked in the documentation.

2. **Immediate Effect**: Some endpoints apply changes immediately (e.g., sensor enabled state, byte offsets).

3. **Thread Safety**: Assembly data access is thread-safe using mutexes.

4. **Caching**: Some endpoints use caching to avoid frequent NVS reads (e.g., sensor enabled state, byte offsets).

5. **Validation**: All endpoints validate input parameters before processing.

6. **NVS Persistence**: Configuration changes are persisted to Non-Volatile Storage (NVS) and survive reboots.

---

## Example Usage

### Using curl

```bash
# Get sensor status
curl http://172.16.82.99/api/status

# Enable sensor
curl -X POST http://172.16.82.99/api/sensor/enabled \
  -H "Content-Type: application/json" \
  -d '{"enabled": true}'

# Set IP configuration
curl -X POST http://172.16.82.99/api/ipconfig \
  -H "Content-Type: application/json" \
  -d '{"use_dhcp": false, "ip_address": "192.168.1.100", "netmask": "255.255.255.0"}'

# Upload firmware
curl -X POST http://172.16.82.99/api/ota/update \
  -F "file=@firmware.bin"
```

### Using JavaScript (fetch)

```javascript
// Get sensor status
fetch('/api/status')
  .then(r => r.json())
  .then(data => console.log(data));

// Enable sensor
fetch('/api/sensor/enabled', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ enabled: true })
})
  .then(r => r.json())
  .then(data => console.log(data));
```

