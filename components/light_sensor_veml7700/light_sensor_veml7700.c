#include <stdio.h>
#include <i2c_master.h>
#include "light_sensor_veml7700.h"

static const char *TAG = "veml7700_driver";
#define VEML7700_I2C_ADDR 0x10
#define VEML7700_REG_CONFIG 0x00
#define VEML7700_REG_DATA 0x04

// Config: Integration time 100ms, Gain 1/8
#define VEML7700_CONFIG_VAL 0x00 

int light_sensor_veml7700_init(int i2c_port) {
    uint8_t config = VEML7700_CONFIG_VAL;
    if (i2c_master_write_to_device(i2c_port, VEML7700_I2C_ADDR, &config, 1, 100) != 0) {
        printf("%s: Failed to configure sensor at 0x%02X\\n", TAG, VEML7700_I2C_ADDR);
        return -1;
    }
    return 0;
}

int light_sensor_veml7700_get_lux(int i2c_port, float *lux) {
    uint8_t reg = VEML7700_REG_DATA;
    uint8_t data[2];

    // Write register address to read
    if (i2c_master_write_to_device(i2c_port, VEML7700_I2C_ADDR, &reg, 1, 100) != 0) {
        return -1;
    }

    if (i2c_master_read_from_device(i2c_port, VEML7700_I2C_ADDR, data, 2, 100) != 0) {
        return -1;
    }

    uint16_t count = (data[1] << 8) | data[0];
    
    // For 100ms integration and 1/8 gain:
    // Lux = count * 0.043 (approximate)
    if (lux) {
        *lux = (float)count * 0.043f;
    }

    return 0;
}
