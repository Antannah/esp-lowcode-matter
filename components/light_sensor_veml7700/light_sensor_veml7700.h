#pragma once

#include <stdint.h>

/**
 * @brief Initialize the VEML7700 light sensor.
 * 
 * @param i2c_port The I2C port to use.
 * @return 0 on success, non-zero on failure.
 */
int light_sensor_veml7700_init(int i2c_port);

/**
 * @brief Read the illuminance (Lux) from the VEML7700 sensor.
 * 
 * @param i2c_port The I2C port to use.
 * @param lux Pointer to store the illuminance value.
 * @return 0 on success, non-zero on failure.
 */
int light_sensor_veml7700_get_lux(int i2c_port, float *lux);
