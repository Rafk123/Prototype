// app_config.h
#pragma once

// ==================== НЕОБХОДИМЫЕ БИБЛИОТЕКИ ====================
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

// ==================== ПИНЫ ====================
#define SD_CS   GPIO_NUM_10
#define SD_MOSI GPIO_NUM_11
#define SD_MISO GPIO_NUM_13
#define SD_CLK  GPIO_NUM_12

// ========== I2S пины для Master (I2S_NUM_0) ==========
#define I2S_BCLK_MASTER_PIN    GPIO_NUM_47
#define I2S_WS_MASTER_PIN      GPIO_NUM_21

// Пины данных для Master (микрофоны 1 и 2)
#define I2S_DIN_MASTER_LEFT    GPIO_NUM_14
#define I2S_DIN_MASTER_RIGHT   GPIO_NUM_15

// ========== I2S пины для Slave (I2S_NUM_1) ==========
#define I2S_BCLK_SLAVE_PIN     GPIO_NUM_47   // Те же пины, что у мастера
#define I2S_WS_SLAVE_PIN       GPIO_NUM_21

// Пины данных для Slave (микрофоны 3 и 4)
#define I2S_DIN_SLAVE_LEFT     GPIO_NUM_16   // ← ИЗМЕНЕНО: был LED_RED_PIN
#define I2S_DIN_SLAVE_RIGHT    GPIO_NUM_17   // ← ИЗМЕНЕНО: был LED_GREEN_PIN

// Кнопки
#define SYSTEM_BUTTON          GPIO_NUM_19
#define RECORD_BUTTON          GPIO_NUM_20

// Светодиоды (освободили пины 16 и 17, нужно перенести на другие)
#define LED_RED_PIN            GPIO_NUM_18   // ← ИЗМЕНЕНО: был GPIO_NUM_17
#define LED_GREEN_PIN          GPIO_NUM_5    // ← ИЗМЕНЕНО: был GPIO_NUM_16

// ==================== КОНСТАНТЫ ====================
#define MOUNT_POINT "/sdcard"
#define SAMPLE_RATE 16000
#define DMA_BUF_SIZE 128
#define SD_FREQUENCY 20000
#define LONG_PRESS 300
#define NUM_CHANNELS 4
#define COUNTER_FILE_PATH "/sdcard/audio/counter.txt"
#define AUDIO_DIR_PATH "/sdcard/audio"
#define LED_ON          1
#define LED_OFF         0

// ==================== РАСЧЕТНЫЕ КОНСТАНТЫ ====================
#define GAIN_FACTOR 8
#define BYTES_PER_SAMPLE 2
#define BYTE_RATE (SAMPLE_RATE * NUM_CHANNELS * BYTES_PER_SAMPLE)  // ← ИЗМЕНЕНО: учёт 4 каналов

// ==================== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ====================
// ← ИЗМЕНЕНО: 4 хендла вместо одного
extern i2s_chan_handle_t rx_handle_master_left;
extern i2s_chan_handle_t rx_handle_master_right;
extern i2s_chan_handle_t rx_handle_slave_left;
extern i2s_chan_handle_t rx_handle_slave_right;

extern sdmmc_card_t *card;
extern FILE *f;
extern uint32_t current_counter;
extern uint8_t wav_header[44];

// ← ИЗМЕНЕНО: 4 буфера вместо одного
extern int32_t i2s_buffer_master_left[DMA_BUF_SIZE];
extern int32_t i2s_buffer_master_right[DMA_BUF_SIZE];
extern int32_t i2s_buffer_slave_left[DMA_BUF_SIZE];
extern int32_t i2s_buffer_slave_right[DMA_BUF_SIZE];

// ← ИЗМЕНЕНО: буфер для 4 каналов (размер * 4)
extern int16_t pcm_buffer_4ch[DMA_BUF_SIZE * 4];

extern char current_filename[64];
extern uint32_t current_file_number;

// ==================== ФЛАГИ ====================
extern bool i2s_enabled;
extern bool sd_mounted;
extern bool file_opened;
extern bool watchdog_enabled;
