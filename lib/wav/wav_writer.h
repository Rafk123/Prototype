#pragma once

#include "app_config.h"
#include "file_counter.h"

esp_err_t create_wav(void);
void write_wav(const int16_t *data, size_t samples);
esp_err_t close_wav(uint32_t);
esp_err_t wav_deinit(void);
uint32_t get_current_file_number(void);
const char* get_current_filename(void);
