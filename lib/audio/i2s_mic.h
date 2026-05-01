// i2s_mic.h
#pragma once

#include "app_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Буфер для непрерывного потока (для записи)
#define STREAM_BUF_SIZE    (DMA_BUF_SIZE * 16)   // достаточно большой кольцевой буфер

typedef struct {
    int16_t *buffer;           // единый массив [4 * STREAM_BUF_SIZE]
    size_t write_idx;          // позиция записи (в 4-канальных сэмплах)
    size_t read_idx;           // позиция чтения
    size_t available;          // количество готовых сэмплов
    SemaphoreHandle_t mutex;
    SemaphoreHandle_t data_sem;// семафор, увеличивающийся при добавлении данных
} stream_buffer_t;

extern stream_buffer_t stream_buf;

// Блок для локализации
#define SYNC_BLOCKS  4

typedef struct {
    int16_t *blocks[4][SYNC_BLOCKS];  // [channel][block_index] -> массив FFT_SIZE int16
    volatile bool block_ready[SYNC_BLOCKS];
    volatile int read_idx;            // блок, который читает локализация
    SemaphoreHandle_t block_sem;      // счётный семафор готовых блоков
    SemaphoreHandle_t mutex;
} sync_block_buffer_t;

extern sync_block_buffer_t block_buf;

esp_err_t i2s_init(void);
esp_err_t i2s_disable(void);

// Получить указатели на готовый синхронный блок (по FFT_SIZE на каждый канал)
esp_err_t i2s_get_localization_block(int16_t **ch1, int16_t **ch2, int16_t **ch3, int16_t **ch4);
void i2s_release_localization_block(void);

// Прочитать непрерывный поток 4-канальных сэмплов (для записи на SD)
// Блокирует, пока не накопится count сэмплов.
esp_err_t i2s_read_continuous(int16_t *dest, size_t count);
