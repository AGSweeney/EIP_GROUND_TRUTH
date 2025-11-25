# LSM6DS3 Assembly Data Format

## Input Assembly 100 — LSM6DS3 Data (20 bytes)

The LSM6DS3 IMU sensor writes data to EtherNet/IP Input Assembly 100 at a configurable byte offset. The data consists of 5 scaled integer values representing orientation angles and pressure readings.

### Data Structure

| Offset | Field | Type | Size | Description |
|--------|-------|------|------|-------------|
| 0 | Roll | INT32 | 4 bytes | Roll angle (degrees × 10000) |
| 4 | Pitch | INT32 | 4 bytes | Pitch angle (degrees × 10000) |
| 8 | GroundAngle | INT32 | 4 bytes | Angle from vertical (degrees × 10000) |
| 12 | BottomPressure | INT32 | 4 bytes | Bottom cylinder pressure (PSI × 1000) |
| 16 | TopPressure | INT32 | 4 bytes | Top cylinder pressure (PSI × 1000) |

**Total Size:** 20 bytes (5 × INT32)

### Byte Order

All values are stored as **little-endian** signed 32-bit integers (`int32_t`).

### Data Format Details

#### Orientation Angles (Roll, Pitch, GroundAngle)

- **Type:** `int32_t` (signed 32-bit integer)
- **Scaling:** Degrees × 10,000
- **Range:** -2,147,483.648° to +2,147,483.647° (theoretical)
- **Practical Range:** -90.0000° to +90.0000° (typical IMU range)
- **Precision:** 0.0001° (0.1 milli-degrees)

**Example:**
- `12.3456°` → `123456` (stored as `0x0001E240`)
- `-5.6789°` → `-56789` (stored as `0xFFFEECEB`)

**Conversion Formula:**
```c
float degrees = (float)scaled_value / 10000.0f;
int32_t scaled = (int32_t)roundf(degrees * 10000.0f);
```

#### Pressure Values (BottomPressure, TopPressure)

- **Type:** `int32_t` (signed 32-bit integer)
- **Scaling:** PSI × 1,000
- **Range:** -2,147,483.648 PSI to +2,147,483.647 PSI (theoretical)
- **Practical Range:** 0.000 PSI to 2,147.483 PSI (typical pressure range)
- **Precision:** 0.001 PSI (1 milli-PSI)

**Example:**
- `123.456 PSI` → `123456` (stored as `0x0001E240`)
- `87.650 PSI` → `87650` (stored as `0x00015602`)

**Conversion Formula:**
```c
float psi = (float)scaled_value / 1000.0f;
int32_t scaled = (int32_t)roundf(psi * 1000.0f);
```

### Sensor Fusion

The orientation angles (Roll, Pitch, GroundAngle) are calculated using **sensor fusion**:

1. **Accelerometer Data:** Used to determine gravity vector and calculate pitch/roll
2. **Gyroscope Data:** Used for high-frequency orientation tracking
3. **Complementary Filter:** Combines accelerometer and gyroscope data to provide stable, drift-free orientation

**Filter Parameters:**
- **Alpha (α):** 0.96 (96% gyroscope, 4% accelerometer)
- **Sample Rate:** 104 Hz
- **Update Rate:** 50 Hz (20ms period)

### Byte Offset Configuration

The starting byte offset for LSM6DS3 data in Input Assembly 100 is **configurable** via:

- **NVS Key:** `lsm6ds3_byte`
- **API Endpoint:** `POST /api/lsm6ds3/byteoffset`
- **Valid Range:** 0-12 (LSM6DS3 uses 20 bytes, so max offset is 32-20=12)
- **Default:** 0 (bytes 0-19)

**Example Configurations:**
- Offset 0: Uses bytes 0-19
- Offset 4: Uses bytes 4-23
- Offset 12: Uses bytes 12-31

### Memory Layout Example

For byte offset = 0:

```
Input Assembly 100 (32 bytes total)
┌─────────────────────────────────────────────────────────┐
│ Byte  │ Field          │ Value (hex)    │ Value (dec)   │
├───────┼────────────────┼────────────────┼──────────────┤
│ 0-3   │ Roll           │ 0x0001E240     │ 123456        │
│ 4-7   │ Pitch          │ 0xFFFEECEB     │ -56789       │
│ 8-11  │ GroundAngle    │ 0x00020C49     │ 134345        │
│ 12-15 │ BottomPressure │ 0x0001E240     │ 123456        │
│ 16-19 │ TopPressure    │ 0x00015602     │ 87650         │
│ 20-31 │ (unused)       │ ...            │ ...           │
└─────────────────────────────────────────────────────────┘
```

### Calibration

The LSM6DS3 gyroscope is calibrated on first boot (or when calibration is triggered via API). Calibration values are stored in NVS and persist across reboots.

**Calibration Process:**
- Device must be kept **completely still** during calibration
- Default: 100 samples at 20ms intervals (~2 seconds)
- Calibration removes gyroscope bias/drift that would cause angle accumulation errors

**API Endpoints:**
- `GET /api/lsm6ds3/calibrate` - Get current calibration status
- `POST /api/lsm6ds3/calibrate` - Trigger new calibration

### Data Flow

```
LSM6DS3 Sensor (I2C)
    ↓
Raw Accelerometer/Gyroscope Readings
    ↓
Calibration Applied (gyro offsets subtracted)
    ↓
Sensor Fusion (Complementary Filter)
    ↓
Roll, Pitch, GroundAngle (degrees)
    ↓
Scaling (× 10000) → INT32
    ↓
Assembly Buffer (little-endian bytes)
    ↓
EtherNet/IP Input Assembly 100
```

### Notes

- **Fallback Sensor:** LSM6DS3 is used as a fallback when MPU6050 is not detected
- **Data Compatibility:** Format matches MPU6050 for compatibility
- **Thread Safety:** Assembly writes are protected by mutex
- **Update Rate:** 50 Hz (20ms period)
- **Endianness:** All values are little-endian (LSB first)

### Related Documentation

- [LSM6DS3 Driver README](driver/README.md)
- [API Endpoints Documentation](../../docs/API_Endpoints.md)
- [MPU6050 Assembly Data](../mpu6050/README.md) (similar format)

