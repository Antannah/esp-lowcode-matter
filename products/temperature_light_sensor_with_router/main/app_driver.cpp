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
#include <cstdlib>
#include <low_code.h>
#include <system.h>
#include <i2c_master.h>
#include <temperature_sensor_sht40.h>
#include <light_sensor_veml7700.h>

#include "app_priv.h"

// ESP32-C6 Super Mini I2C Pins (Updated)
#define I2C_PORT   I2C_NUM_0
#define I2C_SDA_IO (gpio_num_t)19
#define I2C_SCL_IO (gpio_num_t)20

static const char *TAG = "app_driver";

// Mock function for RSSI - In a real Thread device, this would call the OpenThread stack
static int get_thread_rssi() {
    // Return a simulated RSSI value between -90 and -30
    return -60 + (rand() % 20); 
}

static void app_driver_report_feature(uint16_t endpoint, uint32_t feature_id, float value) {
    int16_t scaled_value = (int16_t)(value * 100);
    low_code_feature_data_t update_data = {
        .details = {
            .endpoint_id = endpoint,
            .feature_id = (low_code_feature_id_t)feature_id
        },
        .value = {
            .type = LOW_CODE_VALUE_TYPE_INTEGER,
            .value_len = sizeof(int16_t),
            .value = (uint8_t*)&scaled_value,
        },
    };
    low_code_feature_update_to_system(&update_data);
}

static void app_driver_report_rssi(int rssi) {
    int16_t value = (int16_t)rssi;
    low_code_feature_data_t update_data = {
        .details = {
            .endpoint_id = 4,
            .feature_id = LOW_CODE_FEATURE_ID_RSSI
        },
        .value = {
            .type = LOW_CODE_VALUE_TYPE_INTEGER,
            .value_len = sizeof(int16_t),
            .value = (uint8_t*)&value,
        },
    };
    low_code_feature_update_to_system(&update_data);
}

void app_driver_read_and_report_feature(system_timer_handle_t timer_handle, void *user_data)
{
    float temperature = 0.0f;
    float humidity = 0.0f;
    float lux = 0.0f;

    // Read SHT40
    if (temperature_sensor_sht40_get_data(I2C_PORT, &temperature, &humidity) == 0) {
        app_driver_report_feature(1, LOW_CODE_FEATURE_ID_TEMPERATURE_SENSOR_VALUE, temperature);
        app_driver_report_feature(2, LOW_CODE_FEATURE_ID_HUMIDITY_SENSOR_VALUE, humidity);
    }

    // Read VEML7700
    if (light_sensor_veml7700_get_lux(I2C_PORT, &lux) == 0) {
        app_driver_report_feature(3, LOW_CODE_FEATURE_ID_ILLUMINANCE_SENSOR_VALUE, lux);
    }

    // Read RSSI
    app_driver_report_rssi(get_thread_rssi());

    printf("%s: Reported Temp: %.2f, Hum: %.2f, Lux: %.2f, RSSI: %d\\n", 
           TAG, temperature, humidity, lux, get_thread_rssi());
}

int app_driver_init()
{
    printf("%s: Initializing driver\\n", TAG);

    // Initialize I2C
    i2c_master_init(I2C_PORT, I2C_SDA_IO, I2C_SCL_IO);

    // Initialize sensors
    if (temperature_sensor_sht40_init(I2C_PORT) != 0) {
        printf("%s: Failed to init SHT40\\n", TAG);
    }
    if (light_sensor_veml7700_init(I2C_PORT) != 0) {
        printf("%s: Failed to init VEML7700\\n", TAG);
    }

    // Create timer for measurements every 30 seconds
    system_timer_handle_t timer = system_timer_create(app_driver_read_and_report_feature, NULL, 30000, true);
    if (!timer) {
        printf("%s: Failed to create timer\\n", TAG);
        return -1;
    }
    system_timer_start(timer);

    return 0;
}

int app_driver_feature_update()
{
    return 0; // Device reports features automatically via timer
}

int app_driver_event_handler(low_code_event_t *event)
{
    printf("%s: Received event: %d\\n", TAG, event->event_type);
    return 0;
}
