// Copyright 2024 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <low_code.h>
#include <system.h>
#include <i2c_master.h>
#include <temperature_sensor_sht40.h>
#include <light_sensor_veml7700.h>
#include <light_driver.h>

#include "app_priv.h"

// ESP32-C6 Super Mini I2C Pins: SCL is GPIO 19, SDA is GPIO 20
#define I2C_PORT   I2C_NUM_0
#define I2C_SCL_IO (gpio_num_t)19
#define I2C_SDA_IO (gpio_num_t)20

static const char *TAG = "app_driver";

static void app_driver_report_temperature(float temp)
{
    // Matter Temperature: 0.01 degrees Celsius (int16)
    int16_t temperature = (int16_t)(temp * 100.0f);
    low_code_feature_data_t update_data = {
        .details = {
            .endpoint_id = 1,
            .feature_id = LOW_CODE_FEATURE_ID_TEMPERATURE_SENSOR_VALUE
        },
        .value = {
            .type = LOW_CODE_VALUE_TYPE_INTEGER,
            .value_len = sizeof(int16_t),
            .value = (uint8_t*)&temperature,
        },
    };
    low_code_feature_update_to_system(&update_data);
}

static void app_driver_report_humidity(float hum)
{
    // Matter Relative Humidity: 0.01% (uint16)
    uint16_t humidity = (uint16_t)(hum * 100.0f);
    low_code_feature_data_t update_data = {
        .details = {
            .endpoint_id = 2,
            .feature_id = LOW_CODE_FEATURE_ID_HUMIDITY_SENSOR_VALUE
        },
        .value = {
            .type = LOW_CODE_VALUE_TYPE_UNSIGNED_INTEGER,
            .value_len = sizeof(uint16_t),
            .value = (uint8_t*)&humidity,
        },
    };
    low_code_feature_update_to_system(&update_data);
}

static void app_driver_report_illuminance(uint16_t lux)
{
    // Matter Illuminance: 10000 * log10(lux) + 1 (lux >= 1)
    uint16_t report_value = 0;
    if (lux == 0) {
        report_value = 0;
    } else {
        float val = 10000.0f * log10f((float)lux) + 1.0f;
        if (val > 65534.0f) {
            val = 65534.0f;
        }
        report_value = (uint16_t)val;
    }

    low_code_feature_data_t update_data = {
        .details = {
            .endpoint_id = 3,
            .feature_id = LOW_CODE_FEATURE_ID_ILLUMINANCE_SENSOR_VALUE
        },
        .value = {
            .type = LOW_CODE_VALUE_TYPE_UNSIGNED_INTEGER,
            .value_len = sizeof(uint16_t),
            .value = (uint8_t*)&report_value,
        },
    };
    low_code_feature_update_to_system(&update_data);
}

void app_driver_read_and_report_feature(system_timer_handle_t timer_handle, void *user_data)
{
    float temperature = 0.0f;
    float humidity = 0.0f;
    uint16_t lux = 0;

    // Read SHT40
    if (temperature_sensor_sht40_get_data(I2C_PORT, &temperature, &humidity) == 0) {
        app_driver_report_temperature(temperature);
        app_driver_report_humidity(humidity);
    } else {
        printf("%s: Warning: Failed to read SHT40\n", TAG);
    }

    // Read VEML7700
    if (light_sensor_veml7700_get_lux(I2C_PORT, &lux) == 0) {
        app_driver_report_illuminance(lux);
    } else {
        printf("%s: Warning: Failed to read VEML7700\n", TAG);
    }

    int temp_int = (int)temperature;
    int temp_dec = abs((int)(temperature * 100.0f) % 100);
    int hum_int = (int)humidity;
    int hum_dec = abs((int)(humidity * 100.0f) % 100);

    printf("%s: Reported Temp: %d.%02d C, Hum: %d.%02d %%, Lux: %u\n", 
           TAG, temp_int, temp_dec, hum_int, hum_dec, (unsigned int)lux);
}

#define LED_GPIO_NUM (gpio_num_t)8

static system_timer_handle_t s_led_timer = NULL;
static bool s_led_state = false;
static bool s_ws2812_inited = false;

static void ws2812_set_color(bool on, uint16_t hue, uint8_t sat, uint8_t bri)
{
    if (!s_ws2812_inited) {
        light_driver_config_t cfg = {
            .device_type = LIGHT_DEVICE_TYPE_WS2812,
            .channel_comb = LIGHT_CHANNEL_COMB_3CH_RGB,
            .io_conf = {
                .ws2812_io = {
                    .ctrl_io = LED_GPIO_NUM,
                },
            },
            .min_brightness = 0,
            .max_brightness = 100,
        };
        light_driver_init(&cfg);
        s_ws2812_inited = true;
    }
    if (on) {
        light_driver_set_power(1);
        light_driver_set_hue(hue);
        light_driver_set_saturation(sat);
        light_driver_set_brightness(bri);
    } else {
        light_driver_set_power(0);
    }
}

static void app_driver_led_blink_cb(system_timer_handle_t timer_handle, void *user_data)
{
    s_led_state = !s_led_state;
    // 1) Set standard digital GPIO (active-low: LOW = ON, HIGH = OFF)
    system_digital_write((int)LED_GPIO_NUM, s_led_state ? LOW : HIGH);
    // 2) If it's a WS2812 RGB LED on GPIO8, output blue blink / off
    if (s_led_state) {
        ws2812_set_color(true, 240, 100, 50); // Blue
    } else {
        ws2812_set_color(false, 0, 0, 0);      // Off
    }
}

void app_driver_led_start_blinking(uint32_t period_ms)
{
    if (!s_led_timer) {
        s_led_timer = system_timer_create(app_driver_led_blink_cb, NULL, period_ms, true);
    }
    if (s_led_timer) {
        system_timer_start(s_led_timer);
    }
}

void app_driver_led_stop(bool state_on)
{
    if (s_led_timer) {
        system_timer_stop(s_led_timer);
    }
    system_digital_write((int)LED_GPIO_NUM, state_on ? LOW : HIGH);
    if (state_on) {
        ws2812_set_color(true, 120, 100, 50); // Green when connected
    } else {
        ws2812_set_color(false, 0, 0, 0);     // Off
    }
}

void app_driver_led_init()
{
    system_set_pin_mode((int)LED_GPIO_NUM, OUTPUT);
    app_driver_led_start_blinking(500);
}

int app_driver_init()
{
    printf("%s: Initializing driver (SCL=GPIO%d, SDA=GPIO%d)\n", 
           TAG, (int)I2C_SCL_IO, (int)I2C_SDA_IO);

    // Initialize I2C: i2c_master_init(port, scl_io, sda_io)
    if (i2c_master_init(I2C_PORT, I2C_SCL_IO, I2C_SDA_IO) != 0) {
        printf("%s: Failed to init I2C master\n", TAG);
        return -1;
    }

    // Give I2C bus and sensors time to settle
    system_delay_ms(20);

    // Initialize sensors
    if (temperature_sensor_sht40_init(I2C_PORT) != 0) {
        printf("%s: Failed to init SHT40\n", TAG);
    } else {
        printf("%s: SHT40 initialized successfully\n", TAG);
    }

    if (light_sensor_veml7700_init(I2C_PORT) != 0) {
        printf("%s: Failed to init VEML7700\n", TAG);
    } else {
        printf("%s: VEML7700 initialized successfully\n", TAG);
    }

    // Perform initial read and report
    app_driver_read_and_report_feature(NULL, NULL);

    // Create timer for measurements every 30 seconds
    system_timer_handle_t timer = system_timer_create(app_driver_read_and_report_feature, NULL, 30000, true);
    if (!timer) {
        printf("%s: Failed to create measurement timer\n", TAG);
        return -1;
    }
    system_timer_start(timer);

    return 0;
}

int app_driver_feature_update()
{
    return 0; // Device reports features periodically via timer
}

int app_driver_event_handler(low_code_event_t *event)
{
    printf("%s: Received event: %d\n", TAG, event->event_type);
    switch (event->event_type) {
        case LOW_CODE_EVENT_SETUP_MODE_START:
            printf("%s: Commissioning setup mode started (LED blinking)\n", TAG);
            app_driver_led_start_blinking(500); // Blink every 500ms
            break;
        case LOW_CODE_EVENT_SETUP_SUCCESSFUL:
            printf("%s: Commissioning successful\n", TAG);
            app_driver_led_stop(true); // LED ON
            break;
        case LOW_CODE_EVENT_NETWORK_CONNECTED:
            printf("%s: Thread network connected\n", TAG);
            app_driver_led_stop(false); // LED OFF after connected
            break;
        case LOW_CODE_EVENT_NETWORK_DISCONNECTED:
            printf("%s: Thread network disconnected\n", TAG);
            app_driver_led_start_blinking(1000); // Slow blink
            break;
        default:
            break;
    }
    return 0;
}
