#pragma once

#include <stdint.h>

/**
 * @brief Initialize the SHT40 temperature and humidity sensor.
 * 
 * @param i2c_port The I2C port to use.
 * @return 0 on success, non-zero on failure.
 */
int temperature_sensor_sht40_init(int i2c_port);

/**
 * @brief Read temperature and humidity from the SHT40 sensor.
 * 
 * @param i2c_port The I2C port to use.
 * @param temperature Pointer to store temperature in Celsius.
 * @param humidity Pointer to store relative humidity in %.
 * @return 0 on success, non-zero on failure.
 */
int temperature_sensor_sht40_get_data(int i2c_port, float *temperature, float *humidity);
