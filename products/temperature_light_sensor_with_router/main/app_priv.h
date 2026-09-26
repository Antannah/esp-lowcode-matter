#pragma once

#include <stdint.h>
#include <low_code.h>
#include <system.h>

/* Driver functions */
int app_driver_init();
int app_driver_feature_update();
void app_driver_led_init();
void app_driver_led_start_blinking(uint32_t period_ms);
void app_driver_led_stop(bool state_on);

/* Events handler */
int app_driver_event_handler(low_code_event_t *event);

/* Callbacks from system */
int feature_update_from_system(low_code_feature_data_t *data);
int event_from_system(low_code_event_t *event);
