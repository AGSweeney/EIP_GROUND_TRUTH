#include "mpu6050.h"

#include <math.h>
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MPU6050_I2C_TIMEOUT_MS 100

// Helper function for I2C write-then-read
static esp_err_t write_then_read(i2c_master_dev_handle_t handle, const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_len)
{
    return i2c_master_transmit_receive(handle, tx, tx_len, rx, rx_len, pdMS_TO_TICKS(MPU6050_I2C_TIMEOUT_MS));
}

// Initialize MPU6050 device structure
bool mpu6050_init(mpu6050_t *dev, i2c_master_dev_handle_t i2c_dev)
{
    if (!dev || !i2c_dev)
    {
        return false;
    }
    dev->i2c_dev = i2c_dev;
    dev->accel_fs = MPU6050_ACCEL_FS_2G;
    dev->gyro_fs = MPU6050_GYRO_FS_250DPS;
    dev->accel_scale = 2.0f / 32768.0f;  // Default 2G range
    dev->gyro_scale = 250.0f / 32768.0f; // Default 250 DPS range
    return true;
}

// Write a single register
esp_err_t mpu6050_write_register(mpu6050_t *dev, uint8_t reg, uint8_t value)
{
    if (!dev)
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t payload[2] = {reg, value};
    return i2c_master_transmit(dev->i2c_dev, payload, sizeof(payload), pdMS_TO_TICKS(MPU6050_I2C_TIMEOUT_MS));
}

// Read a single register
esp_err_t mpu6050_read_register(mpu6050_t *dev, uint8_t reg, uint8_t *value)
{
    if (!dev || !value)
    {
        return ESP_ERR_INVALID_ARG;
    }
    return write_then_read(dev->i2c_dev, &reg, 1, value, 1);
}

// Read multiple bytes starting from a register
esp_err_t mpu6050_read_bytes(mpu6050_t *dev, uint8_t reg, uint8_t *buffer, size_t length)
{
    if (!dev || !buffer || length == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    return write_then_read(dev->i2c_dev, &reg, 1, buffer, length);
}

// Modify register bits (read-modify-write)
static esp_err_t modify_register(mpu6050_t *dev, uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t current = 0;
    esp_err_t err = mpu6050_read_register(dev, reg, &current);
    if (err != ESP_OK)
    {
        return err;
    }
    current = (current & ~mask) | (value & mask);
    return mpu6050_write_register(dev, reg, current);
}

// Reset the MPU6050
esp_err_t mpu6050_reset(mpu6050_t *dev)
{
    if (!dev)
    {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = modify_register(dev, MPU6050_REG_PWR_MGMT_1, MPU6050_PWR_MGMT_1_RESET, MPU6050_PWR_MGMT_1_RESET);
    if (err != ESP_OK)
    {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(100)); // Wait for reset to complete
    return ESP_OK;
}

// Wake up the MPU6050 from sleep mode
esp_err_t mpu6050_wake_up(mpu6050_t *dev)
{
    if (!dev)
    {
        return ESP_ERR_INVALID_ARG;
    }
    // Clear sleep bit and set clock source to PLL with X-axis gyro reference
    uint8_t value = MPU6050_PWR_MGMT_1_CLKSEL_PLL_XGYRO;
    return mpu6050_write_register(dev, MPU6050_REG_PWR_MGMT_1, value);
}

// Read WHO_AM_I register
esp_err_t mpu6050_read_who_am_i(mpu6050_t *dev, uint8_t *who_am_i)
{
    return mpu6050_read_register(dev, MPU6050_REG_WHO_AM_I, who_am_i);
}

// Set accelerometer full-scale range
esp_err_t mpu6050_set_accel_config(mpu6050_t *dev, uint8_t fs_range)
{
    if (!dev)
    {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = modify_register(dev, MPU6050_REG_ACCEL_CONFIG, MPU6050_ACCEL_FS_MASK, fs_range & MPU6050_ACCEL_FS_MASK);
    if (err != ESP_OK)
    {
        return err;
    }
    dev->accel_fs = fs_range & MPU6050_ACCEL_FS_MASK;
    
    // Update scale factor
    switch (dev->accel_fs)
    {
        case MPU6050_ACCEL_FS_2G:
            dev->accel_scale = 2.0f / 32768.0f;
            break;
        case MPU6050_ACCEL_FS_4G:
            dev->accel_scale = 4.0f / 32768.0f;
            break;
        case MPU6050_ACCEL_FS_8G:
            dev->accel_scale = 8.0f / 32768.0f;
            break;
        case MPU6050_ACCEL_FS_16G:
            dev->accel_scale = 16.0f / 32768.0f;
            break;
        default:
            dev->accel_scale = 2.0f / 32768.0f;
            break;
    }
    return ESP_OK;
}

// Set gyroscope full-scale range
esp_err_t mpu6050_set_gyro_config(mpu6050_t *dev, uint8_t fs_range)
{
    if (!dev)
    {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = modify_register(dev, MPU6050_REG_GYRO_CONFIG, MPU6050_GYRO_FS_MASK, fs_range & MPU6050_GYRO_FS_MASK);
    if (err != ESP_OK)
    {
        return err;
    }
    dev->gyro_fs = fs_range & MPU6050_GYRO_FS_MASK;
    
    // Update scale factor
    switch (dev->gyro_fs)
    {
        case MPU6050_GYRO_FS_250DPS:
            dev->gyro_scale = 250.0f / 32768.0f;
            break;
        case MPU6050_GYRO_FS_500DPS:
            dev->gyro_scale = 500.0f / 32768.0f;
            break;
        case MPU6050_GYRO_FS_1000DPS:
            dev->gyro_scale = 1000.0f / 32768.0f;
            break;
        case MPU6050_GYRO_FS_2000DPS:
            dev->gyro_scale = 2000.0f / 32768.0f;
            break;
        default:
            dev->gyro_scale = 250.0f / 32768.0f;
            break;
    }
    return ESP_OK;
}

// Set digital low-pass filter bandwidth
esp_err_t mpu6050_set_dlpf(mpu6050_t *dev, uint8_t dlpf_bw)
{
    if (!dev)
    {
        return ESP_ERR_INVALID_ARG;
    }
    return modify_register(dev, MPU6050_REG_CONFIG, MPU6050_DLPF_BW_MASK, dlpf_bw & MPU6050_DLPF_BW_MASK);
}

// Set sample rate divider (sample_rate = 1000 / (1 + divider))
esp_err_t mpu6050_set_sample_rate(mpu6050_t *dev, uint8_t divider)
{
    if (!dev)
    {
        return ESP_ERR_INVALID_ARG;
    }
    return mpu6050_write_register(dev, MPU6050_REG_SMPLRT_DIV, divider);
}

// Read accelerometer data
esp_err_t mpu6050_read_accel(mpu6050_t *dev, mpu6050_accel_t *accel)
{
    if (!dev || !accel)
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t buffer[6] = {0};
    esp_err_t err = mpu6050_read_bytes(dev, MPU6050_REG_ACCEL_XOUT_H, buffer, sizeof(buffer));
    if (err != ESP_OK)
    {
        return err;
    }
    accel->x = (int16_t)((buffer[0] << 8) | buffer[1]);
    accel->y = (int16_t)((buffer[2] << 8) | buffer[3]);
    accel->z = (int16_t)((buffer[4] << 8) | buffer[5]);
    return ESP_OK;
}

// Read gyroscope data
esp_err_t mpu6050_read_gyro(mpu6050_t *dev, mpu6050_gyro_t *gyro)
{
    if (!dev || !gyro)
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t buffer[6] = {0};
    esp_err_t err = mpu6050_read_bytes(dev, MPU6050_REG_GYRO_XOUT_H, buffer, sizeof(buffer));
    if (err != ESP_OK)
    {
        return err;
    }
    gyro->x = (int16_t)((buffer[0] << 8) | buffer[1]);
    gyro->y = (int16_t)((buffer[2] << 8) | buffer[3]);
    gyro->z = (int16_t)((buffer[4] << 8) | buffer[5]);
    return ESP_OK;
}

// Read temperature data
esp_err_t mpu6050_read_temp(mpu6050_t *dev, mpu6050_temp_t *temp)
{
    if (!dev || !temp)
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t buffer[2] = {0};
    esp_err_t err = mpu6050_read_bytes(dev, MPU6050_REG_TEMP_OUT_H, buffer, sizeof(buffer));
    if (err != ESP_OK)
    {
        return err;
    }
    temp->temperature = (int16_t)((buffer[0] << 8) | buffer[1]);
    return ESP_OK;
}

// Enable/disable I2C bypass mode
esp_err_t mpu6050_enable_bypass_mode(mpu6050_t *dev, bool enable)
{
    if (!dev)
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value = enable ? MPU6050_INT_PIN_CFG_BYPASS_EN : 0x00;
    return modify_register(dev, MPU6050_REG_INT_PIN_CFG, MPU6050_INT_PIN_CFG_BYPASS_EN, value);
}

// Read all sensor data (accelerometer, gyroscope, temperature)
esp_err_t mpu6050_read_all(mpu6050_t *dev, mpu6050_sample_t *sample)
{
    if (!dev || !sample)
    {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t err = mpu6050_read_accel(dev, &sample->accel);
    if (err != ESP_OK)
    {
        return err;
    }
    
    err = mpu6050_read_gyro(dev, &sample->gyro);
    if (err != ESP_OK)
    {
        return err;
    }
    
    err = mpu6050_read_temp(dev, &sample->temp);
    if (err != ESP_OK)
    {
        return err;
    }
    
    return ESP_OK;
}

// Configure default settings
esp_err_t mpu6050_configure_default(mpu6050_t *dev)
{
    if (!dev)
    {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Reset device
    esp_err_t err = mpu6050_reset(dev);
    if (err != ESP_OK)
    {
        return err;
    }
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Wake up device
    err = mpu6050_wake_up(dev);
    if (err != ESP_OK)
    {
        return err;
    }
    
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Verify WHO_AM_I
    uint8_t who_am_i = 0;
    err = mpu6050_read_who_am_i(dev, &who_am_i);
    if (err != ESP_OK)
    {
        return err;
    }
    
    if (who_am_i != MPU6050_WHO_AM_I_VALUE)
    {
        return ESP_ERR_NOT_FOUND;
    }
    
    // Configure accelerometer: ±2G range
    err = mpu6050_set_accel_config(dev, MPU6050_ACCEL_FS_2G);
    if (err != ESP_OK)
    {
        return err;
    }
    
    // Configure gyroscope: ±250 DPS range
    err = mpu6050_set_gyro_config(dev, MPU6050_GYRO_FS_250DPS);
    if (err != ESP_OK)
    {
        return err;
    }
    
    // Set DLPF bandwidth to 184Hz
    err = mpu6050_set_dlpf(dev, MPU6050_DLPF_BW_184HZ);
    if (err != ESP_OK)
    {
        return err;
    }
    
    // Set sample rate divider for 100Hz output (1000 / (1 + 9) = 100Hz)
    err = mpu6050_set_sample_rate(dev, 9);
    if (err != ESP_OK)
    {
        return err;
    }
    
    return ESP_OK;
}

// Calculate roll and pitch angles from accelerometer data
esp_err_t mpu6050_calculate_roll_pitch(mpu6050_t *dev, const mpu6050_accel_t *accel, float *roll, float *pitch)
{
    if (!dev || !accel || !roll || !pitch)
    {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Convert raw accelerometer values to g (gravity) units
    float accel_x_g = (float)accel->x * dev->accel_scale;
    float accel_y_g = (float)accel->y * dev->accel_scale;
    float accel_z_g = (float)accel->z * dev->accel_scale;
    
    // Calculate roll: rotation around X-axis
    // Roll = atan2(accel_y, accel_z) * 180 / PI
    *roll = atan2f(accel_y_g, accel_z_g) * 180.0f / (float)M_PI;
    
    // Calculate pitch: rotation around Y-axis
    // Pitch = atan2(-accel_x, sqrt(accel_y^2 + accel_z^2)) * 180 / PI
    float denom = sqrtf(accel_y_g * accel_y_g + accel_z_g * accel_z_g);
    *pitch = atan2f(-accel_x_g, denom) * 180.0f / (float)M_PI;
    
    return ESP_OK;
}

// Calculate absolute ground angle from accelerometer data
esp_err_t mpu6050_calculate_ground_angle(mpu6050_t *dev, const mpu6050_accel_t *accel, float *abs_ground_angle)
{
    if (!dev || !accel || !abs_ground_angle)
    {
        return ESP_ERR_INVALID_ARG;
    }
    
    float roll = 0.0f;
    float pitch = 0.0f;
    
    // First calculate roll and pitch
    esp_err_t err = mpu6050_calculate_roll_pitch(dev, accel, &roll, &pitch);
    if (err != ESP_OK)
    {
        return err;
    }
    
    // Calculate absolute ground angle: sqrt(roll^2 + pitch^2)
    *abs_ground_angle = sqrtf(roll * roll + pitch * pitch);
    
    return ESP_OK;
}

// Calculate complete orientation (roll, pitch, and absolute ground angle)
esp_err_t mpu6050_calculate_orientation(mpu6050_t *dev, const mpu6050_accel_t *accel, mpu6050_orientation_t *orientation)
{
    if (!dev || !accel || !orientation)
    {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Calculate roll and pitch
    esp_err_t err = mpu6050_calculate_roll_pitch(dev, accel, &orientation->roll, &orientation->pitch);
    if (err != ESP_OK)
    {
        return err;
    }
    
    // Calculate absolute ground angle from roll and pitch
    orientation->abs_ground_angle = sqrtf(orientation->roll * orientation->roll + orientation->pitch * orientation->pitch);
    
    return ESP_OK;
}

// Write 16-bit offset value to MPU6050 register pair (high and low bytes)
static esp_err_t write_offset_register(mpu6050_t *dev, uint8_t reg_high, uint8_t reg_low, int16_t offset)
{
    uint8_t high_byte = (uint8_t)((offset >> 8) & 0xFF);
    uint8_t low_byte = (uint8_t)(offset & 0xFF);
    
    esp_err_t err = mpu6050_write_register(dev, reg_high, high_byte);
    if (err != ESP_OK) {
        return err;
    }
    return mpu6050_write_register(dev, reg_low, low_byte);
}

// Set accelerometer offset registers
esp_err_t mpu6050_set_accel_offsets(mpu6050_t *dev, int16_t x, int16_t y, int16_t z)
{
    if (!dev) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t err = write_offset_register(dev, MPU6050_REG_XA_OFFSET_H, MPU6050_REG_XA_OFFSET_L, x);
    if (err != ESP_OK) return err;
    
    err = write_offset_register(dev, MPU6050_REG_YA_OFFSET_H, MPU6050_REG_YA_OFFSET_L, y);
    if (err != ESP_OK) return err;
    
    return write_offset_register(dev, MPU6050_REG_ZA_OFFSET_H, MPU6050_REG_ZA_OFFSET_L, z);
}

// Set gyroscope offset registers
esp_err_t mpu6050_set_gyro_offsets(mpu6050_t *dev, int16_t x, int16_t y, int16_t z)
{
    if (!dev) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t err = write_offset_register(dev, MPU6050_REG_XG_OFFSET_H, MPU6050_REG_XG_OFFSET_L, x);
    if (err != ESP_OK) return err;
    
    err = write_offset_register(dev, MPU6050_REG_YG_OFFSET_H, MPU6050_REG_YG_OFFSET_L, y);
    if (err != ESP_OK) return err;
    
    return write_offset_register(dev, MPU6050_REG_ZG_OFFSET_H, MPU6050_REG_ZG_OFFSET_L, z);
}

