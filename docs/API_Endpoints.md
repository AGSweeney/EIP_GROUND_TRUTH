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
  "angle_deg": 12.34,
  "pressure1_psi": 123.4,
  "pressure2_psi": 87.6,
  "temperature_c": 25.5
}
```

**Fields:**
- `enabled`: Whether MPU6050 is enabled
- `angle_deg`: Fused angle from vertical in degrees
- `pressure1_psi`: Cylinder 1 pressure in PSI
- `pressure2_psi`: Cylinder 2 pressure in PSI
- `temperature_c`: MPU6050 internal temperature in Celsius

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

**Note:** MPU6050 data is fixed at bytes 0-15 (16 bytes) in Input Assembly 100. The byte offset is not configurable.

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

