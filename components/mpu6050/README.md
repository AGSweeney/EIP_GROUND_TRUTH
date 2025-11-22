# MPU6050 ESP-IDF Component

Complete implementation for the InvenSense MPU6050 6-axis motion sensor (3-axis accelerometer, 3-axis gyroscope). This component provides I²C register access and high-level sensor reading functions for ESP-IDF projects, including orientation and ground angle calculations.

## Directory Layout

```
components/mpu6050/
├── CMakeLists.txt
├── include/
│   └── mpu6050.h
├── mpu6050.c
└── README.md
```

## Features

- **Accelerometer**: Configurable full-scale ranges (±2G, ±4G, ±8G, ±16G)
- **Gyroscope**: Configurable full-scale ranges (±250, ±500, ±1000, ±2000 DPS)
- **Temperature sensor**: Internal temperature measurement
- **Digital Low-Pass Filter**: Configurable bandwidth (5Hz, 10Hz, 21Hz, 44Hz, 94Hz, 184Hz, 260Hz)
- **Sample Rate**: Configurable divider for output data rate
- **Orientation Calculation**: Roll, pitch, and absolute ground angle from accelerometer data

## Public API

### Initialization and Configuration

```c
bool mpu6050_init(mpu6050_t *dev, i2c_master_dev_handle_t i2c_dev);
esp_err_t mpu6050_configure_default(mpu6050_t *dev);
esp_err_t mpu6050_reset(mpu6050_t *dev);
esp_err_t mpu6050_wake_up(mpu6050_t *dev);
esp_err_t mpu6050_read_who_am_i(mpu6050_t *dev, uint8_t *who_am_i);
```

### Register Access

```c
esp_err_t mpu6050_write_register(mpu6050_t *dev, uint8_t reg, uint8_t value);
esp_err_t mpu6050_read_register(mpu6050_t *dev, uint8_t reg, uint8_t *value);
esp_err_t mpu6050_read_bytes(mpu6050_t *dev, uint8_t reg, uint8_t *buffer, size_t length);
```

### Sensor Configuration

```c
esp_err_t mpu6050_set_accel_config(mpu6050_t *dev, uint8_t fs_range);
esp_err_t mpu6050_set_gyro_config(mpu6050_t *dev, uint8_t fs_range);
esp_err_t mpu6050_set_dlpf(mpu6050_t *dev, uint8_t dlpf_bw);
esp_err_t mpu6050_set_sample_rate(mpu6050_t *dev, uint8_t divider);
```

### Data Reading

```c
esp_err_t mpu6050_read_accel(mpu6050_t *dev, mpu6050_accel_t *accel);
esp_err_t mpu6050_read_gyro(mpu6050_t *dev, mpu6050_gyro_t *gyro);
esp_err_t mpu6050_read_temp(mpu6050_t *dev, mpu6050_temp_t *temp);
esp_err_t mpu6050_read_all(mpu6050_t *dev, mpu6050_sample_t *sample);
```

### I2C Bypass Mode

```c
esp_err_t mpu6050_enable_bypass_mode(mpu6050_t *dev, bool enable);
```

Enables or disables I2C bypass mode, which allows direct access to external I2C devices connected to the MPU6050's auxiliary I2C bus.

### Orientation Calculation

```c
esp_err_t mpu6050_calculate_roll_pitch(mpu6050_t *dev, const mpu6050_accel_t *accel, float *roll, float *pitch);
esp_err_t mpu6050_calculate_ground_angle(mpu6050_t *dev, const mpu6050_accel_t *accel, float *abs_ground_angle);
esp_err_t mpu6050_calculate_orientation(mpu6050_t *dev, const mpu6050_accel_t *accel, mpu6050_orientation_t *orientation);
```

## Example Usage

### Basic Initialization

```c
#include "mpu6050.h"
#include "driver/i2c_master.h"

// Assuming i2c_master_bus_handle_t i2c_bus is already initialized

i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = MPU6050_I2C_ADDR_PRIMARY,
    .scl_speed_hz = 400000,
};

i2c_master_dev_handle_t i2c_dev;
i2c_master_bus_add_device(i2c_bus, &dev_cfg, &i2c_dev);

mpu6050_t imu;
mpu6050_init(&imu, i2c_dev);
ESP_ERROR_CHECK(mpu6050_configure_default(&imu));

// Verify device
uint8_t who_am_i = 0;
ESP_ERROR_CHECK(mpu6050_read_who_am_i(&imu, &who_am_i));
ESP_LOGI("MPU6050", "WHO_AM_I: 0x%02X (expected 0x%02X)", who_am_i, MPU6050_WHO_AM_I_VALUE);
```

### Reading Sensor Data

```c
// Read individual sensors
mpu6050_accel_t accel;
mpu6050_gyro_t gyro;
mpu6050_temp_t temp;

ESP_ERROR_CHECK(mpu6050_read_accel(&imu, &accel));
ESP_ERROR_CHECK(mpu6050_read_gyro(&imu, &gyro));
ESP_ERROR_CHECK(mpu6050_read_temp(&imu, &temp));

ESP_LOGI("MPU6050", "Accel: X=%d Y=%d Z=%d", accel.x, accel.y, accel.z);
ESP_LOGI("MPU6050", "Gyro:  X=%d Y=%d Z=%d", gyro.x, gyro.y, gyro.z);
ESP_LOGI("MPU6050", "Temp:  %d", temp.temperature);

// Or read all at once
mpu6050_sample_t sample;
ESP_ERROR_CHECK(mpu6050_read_all(&imu, &sample));
```

### Converting Raw Values to Physical Units

```c
// Accelerometer: convert to g (gravity)
float accel_x_g = (float)sample.accel.x * imu.accel_scale;
float accel_y_g = (float)sample.accel.y * imu.accel_scale;
float accel_z_g = (float)sample.accel.z * imu.accel_scale;

// Gyroscope: convert to degrees per second
float gyro_x_dps = (float)sample.gyro.x * imu.gyro_scale;
float gyro_y_dps = (float)sample.gyro.y * imu.gyro_scale;
float gyro_z_dps = (float)sample.gyro.z * imu.gyro_scale;

// Temperature: convert to Celsius
// T = (TEMP_OUT / 340) + 36.53
float temp_c = ((float)sample.temp.temperature / 340.0f) + 36.53f;
```

### Calculating Orientation and Ground Angle

```c
// Read accelerometer data
mpu6050_accel_t accel;
ESP_ERROR_CHECK(mpu6050_read_accel(&imu, &accel));

// Calculate roll and pitch individually
float roll = 0.0f;
float pitch = 0.0f;
ESP_ERROR_CHECK(mpu6050_calculate_roll_pitch(&imu, &accel, &roll, &pitch));
ESP_LOGI("MPU6050", "Roll: %.2f°, Pitch: %.2f°", roll, pitch);

// Calculate absolute ground angle only
float abs_ground_angle = 0.0f;
ESP_ERROR_CHECK(mpu6050_calculate_ground_angle(&imu, &accel, &abs_ground_angle));
ESP_LOGI("MPU6050", "Absolute Ground Angle: %.2f°", abs_ground_angle);

// Or calculate all orientation data at once
mpu6050_orientation_t orientation;
ESP_ERROR_CHECK(mpu6050_calculate_orientation(&imu, &accel, &orientation));
ESP_LOGI("MPU6050", "Roll: %.2f°, Pitch: %.2f°, Ground Angle: %.2f°",
         orientation.roll, orientation.pitch, orientation.abs_ground_angle);
```

The absolute ground angle (`abs_ground_angle`) represents the total tilt angle relative to the horizontal plane, calculated as `sqrt(roll² + pitch²)`. This is useful for applications that need to know the overall tilt regardless of the direction.

### Custom Configuration

```c
// Set accelerometer to ±8G range
ESP_ERROR_CHECK(mpu6050_set_accel_config(&imu, MPU6050_ACCEL_FS_8G));

// Set gyroscope to ±1000 DPS range
ESP_ERROR_CHECK(mpu6050_set_gyro_config(&imu, MPU6050_GYRO_FS_1000DPS));

// Set DLPF bandwidth to 44Hz
ESP_ERROR_CHECK(mpu6050_set_dlpf(&imu, MPU6050_DLPF_BW_44HZ));

// Set sample rate to 200Hz (1000 / (1 + 4) = 200Hz)
ESP_ERROR_CHECK(mpu6050_set_sample_rate(&imu, 4));
```

## I2C Addresses

- **MPU6050 Primary**: `0x68` (AD0 pin LOW)
- **MPU6050 Secondary**: `0x69` (AD0 pin HIGH)

## Notes

- The MPU6050 is a 6-axis IMU (no magnetometer), making it simpler and more cost-effective than the MPU9250.
- The default configuration sets up the sensor for 100Hz output rate with moderate filtering (184Hz DLPF bandwidth).
- For highest performance, consider using FIFO mode or interrupt-driven data collection instead of polling.
- Temperature sensor provides internal die temperature, useful for temperature compensation of other sensors.
- The device supports sleep mode and can be woken up using `mpu6050_wake_up()`.
- Orientation calculations use accelerometer data only and provide roll, pitch, and absolute ground angle.
- I2C bypass mode allows direct access to external I2C devices (like magnetometers) connected to the auxiliary I2C bus.
- The MPU6050 register map is compatible with MPU6500 and similar sensors, making this library potentially usable with those devices as well.

## Register Definitions

All MPU6050 register addresses and bit definitions are provided in the header file. Refer to the InvenSense MPU6050 datasheet for detailed register descriptions.

## Error Handling

All functions return `esp_err_t` with appropriate error codes:
- `ESP_OK`: Success
- `ESP_ERR_INVALID_ARG`: Invalid argument (NULL pointer, invalid parameter, etc.)
- `ESP_ERR_NOT_FOUND`: Device not found (wrong WHO_AM_I value)
- `ESP_ERR_TIMEOUT`: I2C communication timeout
- `ESP_FAIL`: General failure (I2C communication error)

## Comparison with MPU9250

The MPU6050 is similar to the MPU9250 but lacks the integrated magnetometer:
- **MPU6050**: 6-axis (accelerometer + gyroscope)
- **MPU9250**: 9-axis (accelerometer + gyroscope + magnetometer)

Both libraries share the same API structure for accelerometer, gyroscope, and orientation calculations, making it easy to switch between sensors. The MPU6050 is ideal when magnetometer data is not needed or when using an external magnetometer via I2C bypass mode.

