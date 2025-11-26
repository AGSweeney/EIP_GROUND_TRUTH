/**
 * @file mpu6050.h
 * @brief MPU6050 6-axis accelerometer and gyroscope driver
 * 
 * This driver provides an interface to control the MPU6050 6-axis motion sensor.
 * The MPU6050 combines a 3-axis accelerometer and 3-axis gyroscope on a single chip.
 * 
 * Features:
 * - 3-axis accelerometer (2g, 4g, 8g, 16g ranges)
 * - 3-axis gyroscope (250, 500, 1000, 2000 DPS ranges)
 * - Digital low-pass filter (DLPF)
 * - Temperature sensor
 * - I2C interface
 * - Roll, pitch, and ground angle calculations
 * 
 * @note I2C address: 0x68 (primary) or 0x69 (secondary, AD0 pin high)
 */

#ifndef MPU6050_H
#define MPU6050_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Primary I2C address for MPU6050 (AD0 pin low)
 */
#define MPU6050_I2C_ADDR_PRIMARY   0x68

/**
 * @brief Secondary I2C address for MPU6050 (AD0 pin high)
 */
#define MPU6050_I2C_ADDR_SECONDARY 0x69

/**
 * @brief MPU6050 Register Map
 * @{
 */

/** @brief Sample rate divider register */
#define MPU6050_REG_SMPLRT_DIV      0x19
/** @brief Configuration register (DLPF) */
#define MPU6050_REG_CONFIG          0x1A
/** @brief Gyroscope configuration register */
#define MPU6050_REG_GYRO_CONFIG     0x1B
/** @brief Accelerometer configuration register */
#define MPU6050_REG_ACCEL_CONFIG    0x1C
#define MPU6050_REG_FF_THR          0x1D
#define MPU6050_REG_FF_DUR          0x1E
#define MPU6050_REG_MOT_THR         0x1F
#define MPU6050_REG_MOT_DUR         0x20
#define MPU6050_REG_ZRMOT_THR       0x21
#define MPU6050_REG_ZRMOT_DUR       0x22
#define MPU6050_REG_FIFO_EN         0x23
#define MPU6050_REG_I2C_MST_CTRL    0x24
#define MPU6050_REG_I2C_SLV0_ADDR   0x25
#define MPU6050_REG_I2C_SLV0_REG    0x26
#define MPU6050_REG_I2C_SLV0_CTRL   0x27
#define MPU6050_REG_I2C_SLV1_ADDR   0x28
#define MPU6050_REG_I2C_SLV1_REG    0x29
#define MPU6050_REG_I2C_SLV1_CTRL   0x2A
#define MPU6050_REG_I2C_SLV2_ADDR   0x2B
#define MPU6050_REG_I2C_SLV2_REG    0x2C
#define MPU6050_REG_I2C_SLV2_CTRL   0x2D
#define MPU6050_REG_I2C_SLV3_ADDR   0x2E
#define MPU6050_REG_I2C_SLV3_REG    0x2F
#define MPU6050_REG_I2C_SLV3_CTRL   0x30
#define MPU6050_REG_I2C_SLV4_ADDR   0x31
#define MPU6050_REG_I2C_SLV4_REG    0x32
#define MPU6050_REG_I2C_SLV4_DO     0x33
#define MPU6050_REG_I2C_SLV4_CTRL   0x34
#define MPU6050_REG_I2C_SLV4_DI     0x35
#define MPU6050_REG_I2C_MST_STATUS  0x36
#define MPU6050_REG_INT_PIN_CFG     0x37
#define MPU6050_REG_INT_ENABLE      0x38
#define MPU6050_REG_DMP_INT_STATUS  0x39
#define MPU6050_REG_INT_STATUS      0x3A
#define MPU6050_REG_ACCEL_XOUT_H    0x3B
#define MPU6050_REG_ACCEL_XOUT_L    0x3C
#define MPU6050_REG_ACCEL_YOUT_H    0x3D
#define MPU6050_REG_ACCEL_YOUT_L    0x3E
#define MPU6050_REG_ACCEL_ZOUT_H    0x3F
#define MPU6050_REG_ACCEL_ZOUT_L    0x40
#define MPU6050_REG_TEMP_OUT_H      0x41
#define MPU6050_REG_TEMP_OUT_L      0x42
#define MPU6050_REG_GYRO_XOUT_H     0x43
#define MPU6050_REG_GYRO_XOUT_L     0x44
#define MPU6050_REG_GYRO_YOUT_H     0x45
#define MPU6050_REG_GYRO_YOUT_L     0x46
#define MPU6050_REG_GYRO_ZOUT_H     0x47
#define MPU6050_REG_GYRO_ZOUT_L     0x48
#define MPU6050_REG_EXT_SENS_DATA_00 0x49
#define MPU6050_REG_MOT_DETECT_STATUS 0x61
#define MPU6050_REG_I2C_SLV0_DO     0x63
#define MPU6050_REG_I2C_SLV1_DO     0x64
#define MPU6050_REG_I2C_SLV2_DO     0x65
#define MPU6050_REG_I2C_SLV3_DO     0x66
#define MPU6050_REG_I2C_MST_DELAY_CTRL 0x67
#define MPU6050_REG_SIGNAL_PATH_RESET  0x68
#define MPU6050_REG_MOT_DETECT_CTRL    0x69
#define MPU6050_REG_USER_CTRL       0x6A
#define MPU6050_REG_PWR_MGMT_1      0x6B
#define MPU6050_REG_PWR_MGMT_2      0x6C
#define MPU6050_REG_FIFO_COUNTH     0x72
#define MPU6050_REG_FIFO_COUNTL     0x73
#define MPU6050_REG_FIFO_R_W        0x74
#define MPU6050_REG_WHO_AM_I        0x75
#define MPU6050_REG_XA_OFFSET_H     0x77
#define MPU6050_REG_XA_OFFSET_L     0x78
#define MPU6050_REG_YA_OFFSET_H     0x7A
#define MPU6050_REG_YA_OFFSET_L     0x7B
#define MPU6050_REG_ZA_OFFSET_H     0x7D
#define MPU6050_REG_ZA_OFFSET_L     0x7E
#define MPU6050_REG_XG_OFFSET_H     0x13
#define MPU6050_REG_XG_OFFSET_L     0x14
#define MPU6050_REG_YG_OFFSET_H     0x15
#define MPU6050_REG_YG_OFFSET_L     0x16
#define MPU6050_REG_ZG_OFFSET_H     0x17
#define MPU6050_REG_ZG_OFFSET_L     0x18

/** @} */

/**
 * @brief Register bit definitions
 * @{
 */

/** @brief Power management 1 - Sleep bit */
#define MPU6050_PWR_MGMT_1_SLEEP    0x40
#define MPU6050_PWR_MGMT_1_RESET     0x80
#define MPU6050_PWR_MGMT_1_CLKSEL_MASK 0x07
#define MPU6050_PWR_MGMT_1_CLKSEL_PLL_XGYRO 0x01

#define MPU6050_INT_PIN_CFG_BYPASS_EN 0x02
#define MPU6050_INT_PIN_CFG_INT_LEVEL 0x80

#define MPU6050_USER_CTRL_I2C_MST_EN 0x20
#define MPU6050_USER_CTRL_I2C_MST_RST 0x02
#define MPU6050_USER_CTRL_FIFO_RST  0x04
#define MPU6050_USER_CTRL_DMP_RST   0x08

#define MPU6050_I2C_MST_CTRL_I2C_MST_CLK_MASK 0x0F
#define MPU6050_I2C_MST_CTRL_I2C_MST_P_NSR    0x10
#define MPU6050_I2C_MST_CTRL_SLV_3_FIFO_EN   0x20
#define MPU6050_I2C_MST_CTRL_WAIT_FOR_ES     0x40
#define MPU6050_I2C_MST_CTRL_MULT_MST_EN    0x80

#define MPU6050_I2C_SLV0_CTRL_EN            0x80
#define MPU6050_I2C_SLV0_CTRL_LENGTH_MASK  0x0F

// WHO_AM_I values
#define MPU6050_WHO_AM_I_VALUE       0x68

// Accelerometer full-scale range
#define MPU6050_ACCEL_FS_2G         0x00
#define MPU6050_ACCEL_FS_4G         0x08
#define MPU6050_ACCEL_FS_8G         0x10
#define MPU6050_ACCEL_FS_16G        0x18
#define MPU6050_ACCEL_FS_MASK       0x18

// Gyroscope full-scale range
#define MPU6050_GYRO_FS_250DPS      0x00
#define MPU6050_GYRO_FS_500DPS      0x08
#define MPU6050_GYRO_FS_1000DPS     0x10
#define MPU6050_GYRO_FS_2000DPS     0x18
#define MPU6050_GYRO_FS_MASK        0x18

// DLPF (Digital Low Pass Filter) bandwidth
#define MPU6050_DLPF_BW_260HZ       0x00
#define MPU6050_DLPF_BW_184HZ       0x01
#define MPU6050_DLPF_BW_94HZ        0x02
#define MPU6050_DLPF_BW_44HZ        0x03
#define MPU6050_DLPF_BW_21HZ        0x04
#define MPU6050_DLPF_BW_10HZ        0x05
#define MPU6050_DLPF_BW_5HZ         0x06
#define MPU6050_DLPF_BW_MASK        0x07

/** @brief Sample rate divider maximum value */
#define MPU6050_SAMPLE_RATE_DIV_MAX 255

/** @} */

/**
 * @brief Data structures
 * @{
 */

/**
 * @brief Accelerometer data structure
 */
typedef struct {
    int16_t x;  /**< X-axis acceleration (raw value) */
    int16_t y;  /**< Y-axis acceleration (raw value) */
    int16_t z;  /**< Z-axis acceleration (raw value) */
} mpu6050_accel_t;

/**
 * @brief Gyroscope data structure
 */
typedef struct {
    int16_t x;  /**< X-axis angular velocity (raw value) */
    int16_t y;  /**< Y-axis angular velocity (raw value) */
    int16_t z;  /**< Z-axis angular velocity (raw value) */
} mpu6050_gyro_t;

/**
 * @brief Temperature data structure
 */
typedef struct {
    int16_t temperature;  /**< Temperature reading (raw value) */
} mpu6050_temp_t;

/**
 * @brief Combined sensor sample structure
 */
typedef struct {
    mpu6050_accel_t accel;  /**< Accelerometer data */
    mpu6050_gyro_t gyro;   /**< Gyroscope data */
    mpu6050_temp_t temp;   /**< Temperature data */
} mpu6050_sample_t;

/**
 * @brief Orientation angles structure
 */
typedef struct {
    float roll;              /**< Roll angle in degrees */
    float pitch;             /**< Pitch angle in degrees */
    float abs_ground_angle;  /**< Absolute ground angle in degrees (calculated from roll and pitch) */
} mpu6050_orientation_t;

/**
 * @brief MPU6050 device handle structure
 */
typedef struct {
    i2c_master_dev_handle_t i2c_dev;  /**< I2C device handle */
    uint8_t accel_fs;                  /**< Accelerometer full-scale range setting */
    uint8_t gyro_fs;                   /**< Gyroscope full-scale range setting */
    float accel_scale;                 /**< Accelerometer scale factor for conversion */
    float gyro_scale;                  /**< Gyroscope scale factor for conversion */
} mpu6050_t;

/** @} */

/**
 * @brief Function prototypes
 * @{
 */

/**
 * @brief Initialize MPU6050 device structure
 * 
 * Initializes the device handle with default settings:
 * - Accelerometer: 2G range
 * - Gyroscope: 250 DPS range
 * 
 * @param dev Pointer to MPU6050 device structure
 * @param i2c_dev I2C device handle (must be initialized)
 * @return true on success, false on error
 */
bool mpu6050_init(mpu6050_t *dev, i2c_master_dev_handle_t i2c_dev);

/**
 * @brief Write a single register
 * 
 * @param dev Pointer to MPU6050 device structure
 * @param reg Register address
 * @param value Value to write
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_write_register(mpu6050_t *dev, uint8_t reg, uint8_t value);

/**
 * @brief Read a single register
 * 
 * @param dev Pointer to MPU6050 device structure
 * @param reg Register address
 * @param value Pointer to store read value
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_read_register(mpu6050_t *dev, uint8_t reg, uint8_t *value);

/**
 * @brief Read multiple bytes starting from a register
 * 
 * @param dev Pointer to MPU6050 device structure
 * @param reg Starting register address
 * @param buffer Buffer to store read data
 * @param length Number of bytes to read
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_read_bytes(mpu6050_t *dev, uint8_t reg, uint8_t *buffer, size_t length);

/**
 * @brief Configure MPU6050 with default settings
 * 
 * Sets up the device with recommended default configuration:
 * - Wake up from sleep
 * - Set accelerometer to 2G range
 * - Set gyroscope to 250 DPS range
 * - Configure DLPF
 * - Set sample rate
 * 
 * @param dev Pointer to MPU6050 device structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_configure_default(mpu6050_t *dev);

/**
 * @brief Reset MPU6050 device
 * 
 * Performs a software reset of the MPU6050.
 * 
 * @param dev Pointer to MPU6050 device structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_reset(mpu6050_t *dev);

/**
 * @brief Wake up MPU6050 from sleep mode
 * 
 * @param dev Pointer to MPU6050 device structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_wake_up(mpu6050_t *dev);

/**
 * @brief Read WHO_AM_I register
 * 
 * Reads the device ID register to verify communication.
 * Expected value: 0x68
 * 
 * @param dev Pointer to MPU6050 device structure
 * @param who_am_i Pointer to store WHO_AM_I value
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_read_who_am_i(mpu6050_t *dev, uint8_t *who_am_i);

/**
 * @brief Set accelerometer full-scale range
 * 
 * @param dev Pointer to MPU6050 device structure
 * @param fs_range Full-scale range (MPU6050_ACCEL_FS_2G, MPU6050_ACCEL_FS_4G, etc.)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_set_accel_config(mpu6050_t *dev, uint8_t fs_range);

/**
 * @brief Set gyroscope full-scale range
 * 
 * @param dev Pointer to MPU6050 device structure
 * @param fs_range Full-scale range (MPU6050_GYRO_FS_250DPS, MPU6050_GYRO_FS_500DPS, etc.)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_set_gyro_config(mpu6050_t *dev, uint8_t fs_range);

/**
 * @brief Set digital low-pass filter bandwidth
 * 
 * @param dev Pointer to MPU6050 device structure
 * @param dlpf_bw DLPF bandwidth (MPU6050_DLPF_BW_260HZ, MPU6050_DLPF_BW_184HZ, etc.)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_set_dlpf(mpu6050_t *dev, uint8_t dlpf_bw);

/**
 * @brief Set sample rate divider
 * 
 * Sample rate = 1kHz / (1 + divider)
 * 
 * @param dev Pointer to MPU6050 device structure
 * @param divider Sample rate divider (0-255)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_set_sample_rate(mpu6050_t *dev, uint8_t divider);

/**
 * @brief Read accelerometer data
 * 
 * @param dev Pointer to MPU6050 device structure
 * @param accel Pointer to store accelerometer data
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_read_accel(mpu6050_t *dev, mpu6050_accel_t *accel);

/**
 * @brief Read gyroscope data
 * 
 * @param dev Pointer to MPU6050 device structure
 * @param gyro Pointer to store gyroscope data
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_read_gyro(mpu6050_t *dev, mpu6050_gyro_t *gyro);

/**
 * @brief Read temperature data
 * 
 * @param dev Pointer to MPU6050 device structure
 * @param temp Pointer to store temperature data
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_read_temp(mpu6050_t *dev, mpu6050_temp_t *temp);

/**
 * @brief Read all sensor data (accelerometer, gyroscope, temperature)
 * 
 * @param dev Pointer to MPU6050 device structure
 * @param sample Pointer to store combined sensor data
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_read_all(mpu6050_t *dev, mpu6050_sample_t *sample);

/**
 * @brief Enable/disable I2C bypass mode
 * 
 * When enabled, the MPU6050's I2C master is disabled and the auxiliary I2C bus
 * is accessible directly.
 * 
 * @param dev Pointer to MPU6050 device structure
 * @param enable true to enable bypass, false to disable
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_enable_bypass_mode(mpu6050_t *dev, bool enable);

/**
 * @brief Calculate roll and pitch angles from accelerometer data
 * 
 * Calculates roll and pitch angles in degrees from raw accelerometer data.
 * 
 * @param dev Pointer to MPU6050 device structure
 * @param accel Pointer to accelerometer data
 * @param roll Pointer to store roll angle (degrees)
 * @param pitch Pointer to store pitch angle (degrees)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_calculate_roll_pitch(mpu6050_t *dev, const mpu6050_accel_t *accel, float *roll, float *pitch);

/**
 * @brief Calculate absolute ground angle from accelerometer data
 * 
 * Calculates the absolute angle from vertical (ground angle) in degrees.
 * 
 * @param dev Pointer to MPU6050 device structure
 * @param accel Pointer to accelerometer data
 * @param abs_ground_angle Pointer to store ground angle (degrees)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_calculate_ground_angle(mpu6050_t *dev, const mpu6050_accel_t *accel, float *abs_ground_angle);

/**
 * @brief Calculate orientation (roll, pitch, ground angle) from accelerometer data
 * 
 * Calculates all orientation angles from raw accelerometer data.
 * 
 * @param dev Pointer to MPU6050 device structure
 * @param accel Pointer to accelerometer data
 * @param orientation Pointer to store orientation data
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_calculate_orientation(mpu6050_t *dev, const mpu6050_accel_t *accel, mpu6050_orientation_t *orientation);

/**
 * @brief Set accelerometer offset registers
 * 
 * Sets the hardware offset registers for accelerometer calibration.
 * 
 * @param dev Pointer to MPU6050 device structure
 * @param x X-axis offset
 * @param y Y-axis offset
 * @param z Z-axis offset
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_set_accel_offsets(mpu6050_t *dev, int16_t x, int16_t y, int16_t z);

/**
 * @brief Set gyroscope offset registers
 * 
 * Sets the hardware offset registers for gyroscope calibration.
 * 
 * @param dev Pointer to MPU6050 device structure
 * @param x X-axis offset
 * @param y Y-axis offset
 * @param z Z-axis offset
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_set_gyro_offsets(mpu6050_t *dev, int16_t x, int16_t y, int16_t z);

/** @} */
bool mpu6050_init(mpu6050_t *dev, i2c_master_dev_handle_t i2c_dev);
esp_err_t mpu6050_write_register(mpu6050_t *dev, uint8_t reg, uint8_t value);
esp_err_t mpu6050_read_register(mpu6050_t *dev, uint8_t reg, uint8_t *value);
esp_err_t mpu6050_read_bytes(mpu6050_t *dev, uint8_t reg, uint8_t *buffer, size_t length);
esp_err_t mpu6050_configure_default(mpu6050_t *dev);
esp_err_t mpu6050_reset(mpu6050_t *dev);
esp_err_t mpu6050_wake_up(mpu6050_t *dev);
esp_err_t mpu6050_read_who_am_i(mpu6050_t *dev, uint8_t *who_am_i);
esp_err_t mpu6050_set_accel_config(mpu6050_t *dev, uint8_t fs_range);
esp_err_t mpu6050_set_gyro_config(mpu6050_t *dev, uint8_t fs_range);
esp_err_t mpu6050_set_dlpf(mpu6050_t *dev, uint8_t dlpf_bw);
esp_err_t mpu6050_set_sample_rate(mpu6050_t *dev, uint8_t divider);
esp_err_t mpu6050_read_accel(mpu6050_t *dev, mpu6050_accel_t *accel);
esp_err_t mpu6050_read_gyro(mpu6050_t *dev, mpu6050_gyro_t *gyro);
esp_err_t mpu6050_read_temp(mpu6050_t *dev, mpu6050_temp_t *temp);
esp_err_t mpu6050_read_all(mpu6050_t *dev, mpu6050_sample_t *sample);
esp_err_t mpu6050_enable_bypass_mode(mpu6050_t *dev, bool enable);
esp_err_t mpu6050_calculate_roll_pitch(mpu6050_t *dev, const mpu6050_accel_t *accel, float *roll, float *pitch); // Calculates the roll and pitch from the acceleration data
esp_err_t mpu6050_calculate_ground_angle(mpu6050_t *dev, const mpu6050_accel_t *accel, float *abs_ground_angle); // Calculates the absolute ground angle from roll and pitch
esp_err_t mpu6050_calculate_orientation(mpu6050_t *dev, const mpu6050_accel_t *accel, mpu6050_orientation_t *orientation); // Calculates the orientation from roll, pitch, and absolute ground angle
esp_err_t mpu6050_set_accel_offsets(mpu6050_t *dev, int16_t x, int16_t y, int16_t z); // Set accelerometer offset registers
esp_err_t mpu6050_set_gyro_offsets(mpu6050_t *dev, int16_t x, int16_t y, int16_t z); // Set gyroscope offset registers

#ifdef __cplusplus
}
#endif

#endif // MPU6050_H

