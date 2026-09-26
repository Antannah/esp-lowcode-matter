#pragma once

#include <low_code.h>
#include <system.h>

int app_driver_init();
int app_driver_feature_update();
int app_driver_event_handler(low_code_event_t *event);
