#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the VEML7700 light sensor.
 * 
 * @param i2c_port The I2C port to use.
 * @return 0 on success, non-zero on failure.
 */
int light_sensor_veml7700_init(int i2c_port);

/**
 * @brief Read the illuminance (Lux) from the VEML7700 sensor as uint16_t.
 * 
 * @param i2c_port The I2C port to use.
 * @param lux Pointer to store the illuminance value in Lux.
 * @return 0 on success, non-zero on failure.
 */
int light_sensor_veml7700_get_lux(int i2c_port, uint16_t *lux);

#ifdef __cplusplus
}
#endif
