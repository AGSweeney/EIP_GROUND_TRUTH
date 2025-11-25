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

## MPU6050 Sensor Status

### GET /api/mpu6050/status

Get MPU6050 sensor status and current readings.

**Response:**
```json
{
  "enabled": true,
  "roll": 12.3456,
  "pitch": -5.6789,
  "ground_angle": 13.4500,
  "bottom_pressure_psi": 123.456,
  "top_pressure_psi": 87.650,
  "roll_scaled": 123456,
  "pitch_scaled": -56789,
  "ground_angle_scaled": 134500,
  "bottom_pressure_scaled": 123456,
  "top_pressure_scaled": 87650,
  "byte_offset": 0,
  "byte_range_start": 0,
  "byte_range_end": 19
}
```

**Fields:**
- `enabled`: Whether MPU6050 is enabled
- `roll`: Roll angle in degrees
- `pitch`: Pitch angle in degrees
- `ground_angle`: Ground angle from vertical in degrees
- `bottom_pressure_psi`: Bottom cylinder pressure in PSI
- `top_pressure_psi`: Top cylinder pressure in PSI
- `roll_scaled`, `pitch_scaled`, `ground_angle_scaled`: Raw scaled integer values (degrees × 10000)
- `bottom_pressure_scaled`, `top_pressure_scaled`: Raw scaled integer values (PSI × 1000)
- `byte_offset`: Starting byte offset in Input Assembly 100
- `byte_range_start`, `byte_range_end`: Byte range used (20 bytes total)

---

### GET /api/assemblies

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

**Note:** MPU6050 data uses 20 bytes (5 int32_t values: roll, pitch, ground_angle, bottom_pressure, top_pressure) in Input Assembly 100. The byte offset is configurable (0-12).

---

## LSM6DS3 IMU Configuration

The LSM6DS3 is a fallback IMU sensor that is used when MPU6050 is not detected. It provides the same data format and functionality as MPU6050.

### GET /api/lsm6ds3/enabled

Get LSM6DS3 IMU enabled state.

**Response:**
```json
{
  "enabled": true
}
```

### POST /api/lsm6ds3/enabled

Set LSM6DS3 IMU enabled state. Changes take effect immediately.

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
  "message": "LSM6DS3 state saved successfully"
}
```

---

### GET /api/lsm6ds3/byteoffset

Get LSM6DS3 data byte offset in Input Assembly 100.

**Response:**
```json
{
  "start_byte": 0,
  "end_byte": 19,
  "range": "0-19"
}
```

**Fields:**
- `start_byte`: Starting byte position (0-12)
- `end_byte`: Ending byte position (start_byte + 19)
- `range`: Human-readable range string

### POST /api/lsm6ds3/byteoffset

Set LSM6DS3 data byte offset in Input Assembly 100.

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
  "end_byte": 19,
  "range": "0-19",
  "message": "LSM6DS3 byte offset saved successfully"
}
```

**Notes:**
- Valid range: 0-12 (LSM6DS3 uses 20 bytes: 5 int32_t values)
- Changes take effect immediately
- Data format: roll, pitch, ground_angle, bottom_pressure, top_pressure (each as int32_t, scaled: degrees * 10000, pressure * 1000)

---

### GET /api/lsm6ds3/status

Get LSM6DS3 sensor status and current readings.

**Response:**
```json
{
  "enabled": true,
  "roll": 12.34,
  "pitch": -5.67,
  "ground_angle": 13.45,
  "bottom_pressure_psi": 123.4,
  "top_pressure_psi": 87.6,
  "roll_scaled": 123400,
  "pitch_scaled": -56700,
  "ground_angle_scaled": 134500,
  "bottom_pressure_scaled": 123400,
  "top_pressure_scaled": 87600,
  "byte_offset": 0,
  "byte_range_start": 0,
  "byte_range_end": 19
}
```

**Fields:**
- `enabled`: Whether LSM6DS3 is enabled
- `roll`: Roll angle in degrees (float)
- `pitch`: Pitch angle in degrees (float)
- `ground_angle`: Angle from vertical in degrees (float)
- `bottom_pressure_psi`: Bottom cylinder pressure in PSI (float)
- `top_pressure_psi`: Top cylinder pressure in PSI (float)
- `*_scaled`: Raw scaled integer values (degrees * 10000, pressure * 1000)
- `byte_offset`: Current byte offset in Input Assembly 100
- `byte_range_start`: Starting byte of data range
- `byte_range_end`: Ending byte of data range

**Notes:**
- LSM6DS3 is used as a fallback when MPU6050 is not detected
- Data format matches MPU6050 for compatibility
- Uses sensor fusion (complementary filter) for accurate orientation

---

### GET /api/lsm6ds3/calibrate

Get LSM6DS3 gyroscope calibration status and current offset values.

**Response:**
```json
{
  "calibrated": true,
  "gyro_offset_x_mdps": 12743.50,
  "gyro_offset_y_mdps": -31052.70,
  "gyro_offset_z_mdps": -8773.10
}
```

**Fields:**
- `calibrated`: Whether the sensor has been calibrated
- `gyro_offset_x_mdps`: X-axis gyroscope offset in millidegrees per second
- `gyro_offset_y_mdps`: Y-axis gyroscope offset in millidegrees per second
- `gyro_offset_z_mdps`: Z-axis gyroscope offset in millidegrees per second

**Notes:**
- Calibration values are stored in NVS and persist across reboots
- If `calibrated` is `false`, offset values will be 0

---

### POST /api/lsm6ds3/calibrate

Trigger LSM6DS3 gyroscope calibration. **Device must be kept still during calibration.**

**Request (optional parameters):**
```json
{
  "samples": 100,
  "sample_delay_ms": 20
}
```

**Parameters:**
- `samples` (optional): Number of samples to collect (default: 100, range: 1-1000)
- `sample_delay_ms` (optional): Delay between samples in milliseconds (default: 20, range: 1-1000)

**Response:**
```json
{
  "status": "ok",
  "message": "LSM6DS3 calibration complete",
  "calibrated": true,
  "gyro_offset_x_mdps": 12743.50,
  "gyro_offset_y_mdps": -31052.70,
  "gyro_offset_z_mdps": -8773.10,
  "samples": 100,
  "sample_delay_ms": 20
}
```

**Error Response:**
```json
{
  "status": "error",
  "message": "LSM6DS3 calibration failed - sensor may not be initialized",
  "calibrated": false
}
```

**Notes:**
- Calibration takes approximately `samples * sample_delay_ms` milliseconds (default: ~2 seconds)
- Device must be kept completely still during calibration for accurate results
- Calibration values are automatically saved to NVS and will be loaded on next boot
- If sensor is not initialized, returns error status
- Default calibration: 100 samples at 20ms intervals = 2 seconds total

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

