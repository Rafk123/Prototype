#pragma once
#include "app_config.h"
esp_err_t localization_init(void);
float localization_calculate_azimuth(const int16_t *ch1, const int16_t *ch2,
                                     const int16_t *ch3, const int16_t *ch4);
