#include <stdio.h>
#include <string.h>
#include <i2c_master.h>
#include <system.h>
#include "temperature_sensor_sht40.h"

static const char *TAG = "sht40_driver";

#define SHT40_I2C_ADDR              0x44
#define SHT40_CMD_MEASURE_HIGH_PREC 0xFD
#define SHT40_CMD_SOFT_RESET        0x94
#define SHT40_CRC8_POLYNOMIAL       0x31

static uint8_t calculate_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (size_t j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ SHT40_CRC8_POLYNOMIAL;
            } else {
                crc = (crc << 1);
            }
        }
    }
    return crc;
}

int temperature_sensor_sht40_init(int i2c_port)
{
    uint8_t cmd = SHT40_CMD_SOFT_RESET;
    // ticks_to_wait = -1 waits until transaction completes or hardware NACKs
    if (i2c_master_write_to_device(i2c_port, SHT40_I2C_ADDR, &cmd, 1, -1) != 0) {
        printf("%s: Sensor not found at 0x%02X\n", TAG, SHT40_I2C_ADDR);
        return -1;
    }
    system_delay_ms(10);
    return 0;
}

int temperature_sensor_sht40_get_data(int i2c_port, float *temperature, float *humidity)
{
    uint8_t cmd = SHT40_CMD_MEASURE_HIGH_PREC;
    uint8_t data[6] = {0};

    if (i2c_master_write_to_device(i2c_port, SHT40_I2C_ADDR, &cmd, 1, -1) != 0) {
        printf("%s: Failed to send measurement command\n", TAG);
        return -1;
    }

    // High-precision measurement requires typ. 8.2 ms, max. 10 ms
    system_delay_ms(15);

    if (i2c_master_read_from_device(i2c_port, SHT40_I2C_ADDR, data, sizeof(data), -1) != 0) {
        printf("%s: Failed to read measurement data\n", TAG);
        return -1;
    }

    // Verify CRC for temperature
    if (calculate_crc8(&data[0], 2) != data[2]) {
        printf("%s: Temperature CRC error\n", TAG);
        return -1;
    }

    // Verify CRC for humidity
    if (calculate_crc8(&data[3], 2) != data[5]) {
        printf("%s: Humidity CRC error\n", TAG);
        return -1;
    }

    uint16_t t_ticks = ((uint16_t)data[0] << 8) | data[1];
    uint16_t h_ticks = ((uint16_t)data[3] << 8) | data[4];

    if (temperature) {
        *temperature = -45.0f + 175.0f * ((float)t_ticks / 65535.0f);
    }
    if (humidity) {
        float h = -6.0f + 125.0f * ((float)h_ticks / 65535.0f);
        if (h < 0.0f) {
            h = 0.0f;
        } else if (h > 100.0f) {
            h = 100.0f;
        }
        *humidity = h;
    }

    return 0;
}
