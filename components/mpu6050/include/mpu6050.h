#ifndef MPU6050_H
#define MPU6050_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// I2C addresses
#define MPU6050_I2C_ADDR_PRIMARY   0x68
#define MPU6050_I2C_ADDR_SECONDARY 0x69

// MPU6050 Register Map
#define MPU6050_REG_SMPLRT_DIV      0x19
#define MPU6050_REG_CONFIG          0x1A
#define MPU6050_REG_GYRO_CONFIG     0x1B
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

// Register bit definitions
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

// Sample rate divider
#define MPU6050_SAMPLE_RATE_DIV_MAX 255

// Data structures
typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} mpu6050_accel_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} mpu6050_gyro_t;

typedef struct {
    int16_t temperature;
} mpu6050_temp_t;

typedef struct {
    mpu6050_accel_t accel;
    mpu6050_gyro_t gyro;
    mpu6050_temp_t temp;
} mpu6050_sample_t;

typedef struct {
    float roll;           // Roll angle in degrees
    float pitch;          // Pitch angle in degrees
    float abs_ground_angle; // Absolute ground angle in degrees (calculated from roll and pitch)
} mpu6050_orientation_t;

typedef struct {
    i2c_master_dev_handle_t i2c_dev;
    uint8_t accel_fs;
    uint8_t gyro_fs;
    float accel_scale;
    float gyro_scale;
} mpu6050_t;

// Function prototypes
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

