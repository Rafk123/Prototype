// led_manager.h
#pragma once

#include "app_config.h"

esp_err_t led_init(void);

void led_red_on(void);
void led_red_off(void);

void led_green_on(void);
void led_green_off(void);