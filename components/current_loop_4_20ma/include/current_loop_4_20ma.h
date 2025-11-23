/**
 * @file current_loop_4_20ma.h
 * @brief 4-20mA Current Loop Output Driver
 * 
 * This component provides an interface to control 4-20mA current loop outputs
 * using ESP32-P4 PWM output to drive an external XTR111 current loop transmitter IC.
 * 
 * @note This component is currently NOT included in the build.
 *       To enable, uncomment the component registration in CMakeLists.txt
 */

#ifndef CURRENT_LOOP_4_20MA_H
#define CURRENT_LOOP_4_20MA_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize 4-20mA current loop output
 * 
 * Initializes the PWM output on the specified GPIO pin and configures
 * it for 4-20mA current loop control. Sets initial output to 4mA (minimum).
 * 
 * @param pwm_gpio GPIO pin number for PWM output (0-54)
 * @return true if initialization successful, false otherwise
 * 
 * @note Uses LEDC timer 0, channel 0 with 1kHz frequency and 13-bit resolution
 */
bool current_loop_4_20ma_init(int pwm_gpio);

/**
 * @brief Set 4-20mA output current
 * 
 * Sets the current loop output to the specified value in milliamps.
 * Values outside the 4-20mA range will be clamped to valid range.
 * 
 * @param current_ma Desired current in milliamps (4.0 to 20.0)
 * @return true if set successfully, false if not initialized or error occurred
 * 
 * @note Internally converts current to PWM duty cycle:
 *       Voltage = (Current - 4.0) / 16.0 × 2.5V
 *       Duty = (Voltage / 3.3V) × 8191
 */
bool current_loop_4_20ma_set(float current_ma);

/**
 * @brief Get current 4-20mA output setting
 * 
 * Returns the last set current value in milliamps.
 * 
 * @return Current setting in milliamps (float)
 */
float current_loop_4_20ma_get(void);

/**
 * @brief Update from EtherNet/IP Output Assembly data
 * 
 * Updates the 4-20mA output based on a byte value from the EtherNet/IP
 * Output Assembly. Provides linear mapping:
 * - Byte 0 → 4.0 mA
 * - Byte 255 → 20.0 mA
 * 
 * @param assembly_byte Byte value from Output Assembly (0-255)
 *                      Maps linearly to 4-20mA current range
 * 
 * @note Formula: Current (mA) = 4.0 + (byte / 255.0) × 16.0
 */
void current_loop_4_20ma_update_from_assembly(uint8_t assembly_byte);

#ifdef __cplusplus
}
#endif

#endif // CURRENT_LOOP_4_20MA_H

