#include <stdio.h>
#include <string.h>
#include <i2c_master.h>
#include <system.h>
#include "light_sensor_veml7700.h"

static const char *TAG = "veml7700_driver";

#define VEML7700_I2C_ADDR       0x10
#define VEML7700_REG_ALS_CONF   0x00
#define VEML7700_REG_ALS_PSM    0x03
#define VEML7700_REG_ALS_DATA   0x04
#define VEML7700_REG_WHITE_DATA 0x05

// Configuration:
// ALS_GAIN: 1/8 (bits 12:11 = 10 -> 0x1000) - best for room light / lamps up to 120,000 lux
// ALS_IT:   100ms (bits 9:6 = 0000)
// ALS_PERS: 1 (bits 5:4 = 00)
// ALS_INT:  Disabled (bit 1 = 0)
// ALS_SD:   Power ON (bit 0 = 0)
#define VEML7700_CONF_WORD      0x1000

int light_sensor_veml7700_init(int i2c_port)
{
    // 1. First ensure sensor is shut down (SD=1) to reset state machine
    uint8_t sd_buf[3] = { VEML7700_REG_ALS_CONF, 0x01, 0x00 };
    i2c_master_write_to_device(i2c_port, VEML7700_I2C_ADDR, sd_buf, sizeof(sd_buf), -1);
    system_delay_ms(10);

    // 2. Disable Power Saving Mode: ALS_PSM (0x03) -> 0x0000 (PSM_EN = 0)
    uint8_t psm_buf[3] = { VEML7700_REG_ALS_PSM, 0x00, 0x00 };
    i2c_master_write_to_device(i2c_port, VEML7700_I2C_ADDR, psm_buf, sizeof(psm_buf), -1);
    system_delay_ms(5);

    // 3. Configure ALS_CONF (0x00) -> 0x1000 (Active, Gain 1/8, IT 100ms)
    uint8_t conf_buf[3] = {
        VEML7700_REG_ALS_CONF,
        (uint8_t)(VEML7700_CONF_WORD & 0xFF),
        (uint8_t)((VEML7700_CONF_WORD >> 8) & 0xFF)
    };

    if (i2c_master_write_to_device(i2c_port, VEML7700_I2C_ADDR, conf_buf, sizeof(conf_buf), -1) != 0) {
        printf("%s: Sensor not found at 0x%02X\n", TAG, VEML7700_I2C_ADDR);
        return -1;
    }

    // 4. Read back ALS_CONF register to verify it accepted configuration
    uint8_t conf_reg = VEML7700_REG_ALS_CONF;
    uint8_t conf_read[2] = {0};
    i2c_master_write_read_device(i2c_port, VEML7700_I2C_ADDR, &conf_reg, 1, conf_read, sizeof(conf_read), -1);

    // Wait at least 150 ms for the first 100ms measurement cycle to complete
    system_delay_ms(150);
    return 0;
}

int light_sensor_veml7700_get_lux(int i2c_port, uint16_t *lux)
{
    uint8_t reg_als = VEML7700_REG_ALS_DATA;
    uint8_t data_als[2] = {0};

    // Read ALS channel (0x04) via atomic Repeated-Start
    if (i2c_master_write_read_device(i2c_port, VEML7700_I2C_ADDR, &reg_als, 1, data_als, sizeof(data_als), -1) != 0) {
        printf("%s: Failed to read ALS data\n", TAG);
        return -1;
    }

    uint16_t count_als = (uint16_t)data_als[0] | ((uint16_t)data_als[1] << 8);

    if (lux) {
        // At Gain 1/8, IT 100ms: Resolution is 0.4608 lx/cnt = 4608 / 10000
        uint32_t calc = ((uint32_t)count_als * 4608UL) / 10000UL;
        if (calc > 65535UL) {
            calc = 65535UL;
        }
        *lux = (uint16_t)calc;
    }

    return 0;
}
