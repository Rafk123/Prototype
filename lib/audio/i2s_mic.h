// i2s_mic.h
#pragma once

#include "app_config.h"

// ========== Дескрипторы каналов ==========
extern i2s_chan_handle_t rx_handle_master_left;
extern i2s_chan_handle_t rx_handle_master_right;
extern i2s_chan_handle_t rx_handle_slave_left;
extern i2s_chan_handle_t rx_handle_slave_right;

// ========== Буферы для данных ==========
extern int16_t pcm_buffer_4ch[DMA_BUF_SIZE * 4];
extern int32_t i2s_buffer_master_left[DMA_BUF_SIZE];
extern int32_t i2s_buffer_master_right[DMA_BUF_SIZE];
extern int32_t i2s_buffer_slave_left[DMA_BUF_SIZE];
extern int32_t i2s_buffer_slave_right[DMA_BUF_SIZE];

extern bool i2s_enabled;

// ========== Основные функции ==========
esp_err_t i2s_init(void);
esp_err_t i2s_disable(void);

// ========== Функция чтения всех 4 каналов ==========
esp_err_t microphone_read_all_4ch(
    size_t *samples_read
);