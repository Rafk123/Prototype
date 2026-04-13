#pragma once

#include "app_config.h"

esp_err_t counter_init(void);
esp_err_t counter_increment(void);
uint32_t counter_get_current(void);
esp_err_t counter_reset(void);