#pragma once

#include "app_config.h"

esp_err_t i2s_init();
esp_err_t i2s_disable();
esp_err_t microphone_read(size_t*);
esp_err_t microphone_read_with_timeout(size_t *bytes_read, TickType_t timeout);