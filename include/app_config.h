// app_config.h
#pragma once

// ==================== НЕОБХОДИМЫЕ БИБЛИОТЕКИ ====================
#include <stdio.h>
#include <string.h>
#include <math.h>
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
#include "esp_heap_caps.h"

// ======================= ЯДРА =======================
#define MAIN_CORE   PRO_CPU_NUM     // Основное ядро, используемое для периферии и связанных с ними вычислительных задач
#define CNN_CORE    APP_CPU_NUM     // Ядро для инференса, используется только для инициализации модели, FFT, MEL и инференса

// ======================= ПИНЫ =======================
#define SD_CS   GPIO_NUM_10         // Chip Select  - выбор esp устройства периферии
#define SD_MOSI GPIO_NUM_11         // Slave In     - линия передачи данных из esp в sd-card
#define SD_MISO GPIO_NUM_13         // Slave Out    - линия передачи данных из sd-card в esp
#define SD_CLK  GPIO_NUM_12         // Clock        - сигнал синхронизации

// ========= I2S пины для Master (I2S_NUM_0) ==========
#define I2S_BCLK_MASTER_PIN    GPIO_NUM_47      // Bit Clock    - сигнал синхронизации
#define I2S_WS_MASTER_PIN      GPIO_NUM_21      // Word Select  - сигнал для выбора канала

// Пины данных для Master (микрофоны 1 и 2)
#define I2S_DIN_MASTER_LEFT    GPIO_NUM_14      // Data Out     - сигнал цифрового звука
#define I2S_DIN_MASTER_RIGHT   GPIO_NUM_15

// ========== I2S пины для Slave (I2S_NUM_1) ==========
#define I2S_BCLK_SLAVE_PIN     GPIO_NUM_47      // Те же пины, что у мастера
#define I2S_WS_SLAVE_PIN       GPIO_NUM_21

// Пины данных для Slave (микрофоны 3 и 4)
#define I2S_DIN_SLAVE_LEFT     GPIO_NUM_16
#define I2S_DIN_SLAVE_RIGHT    GPIO_NUM_17

#define SYSTEM_BUTTON   GPIO_NUM_19   // Кнопка для включения и выключения системы
#define RECORD_BUTTON   GPIO_NUM_20   // Кнопка для включения и выключения записи аудио

#define LED_RED_PIN     GPIO_NUM_18 // Красный LED - индикация системы
#define LED_GREEN_PIN   GPIO_NUM_5  // Зеленый LED - индикация записи

// ==================== КОНСТАНТЫ ====================
#define MOUNT_POINT "/sdcard"                           // Раздел SD-карты
#define SAMPLE_RATE 16000                               // Частота дискретизации (16 кГц для голоса)
#define DMA_BUF_SIZE 128                                // Размер DMA буфера (количество фреймов)
#define SD_FREQUENCY 20000                              // Частота SPI-SD в кГц
#define LONG_PRESS 300                                  // Длительность долгого нажатия
#define COUNTER_FILE_PATH "/sdcard/audio/counter.txt"   // Текстовый файл, хранящий счет аудиофайлов (метаданные)
#define AUDIO_DIR_PATH "/sdcard/audio"                  // Директория с аудиофайлами
#define LED_ON          1                               // Константа для включенного состояния светодиода
#define LED_OFF         0                               // Константа для выключенного состояния светодиода

// ==================== РАСЧЕТНЫЕ КОНСТАНТЫ ====================
#define GAIN_FACTOR 8                                   // Коэффициент усиления звука
#define BYTES_PER_SAMPLE 2                              // 16 бит = 2 байта
#define BYTE_RATE (SAMPLE_RATE * 4 * BYTES_PER_SAMPLE)      // 

// =================== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ===================
extern i2s_chan_handle_t rx_handle_master_left;
extern i2s_chan_handle_t rx_handle_master_right;
extern i2s_chan_handle_t rx_handle_slave_left;
extern i2s_chan_handle_t rx_handle_slave_right;         // Хендл для программного I2S канала    

extern sdmmc_card_t *card;                              // Указатель на SD-карту
extern FILE *f;                                         // Указатель на файл
extern uint32_t current_counter;                        // Системный счетчик аудиофайлов
extern uint8_t wav_header[44];                          // Заголовок WAV-файла

extern int32_t i2s_buffer_master_left[DMA_BUF_SIZE];
extern int32_t i2s_buffer_master_right[DMA_BUF_SIZE];
extern int32_t i2s_buffer_slave_left[DMA_BUF_SIZE];
extern int32_t i2s_buffer_slave_right[DMA_BUF_SIZE];    // Сырые данные из i2s dma-буфера

extern int16_t pcm_buffer_4ch[DMA_BUF_SIZE * 4];                // Обработанные данные из i2s dma-буфера

extern char current_filename[64];                       // Текущее название аудиофайла
extern uint32_t current_file_number;                    // Нынешний номер записывваемого аудиофайла

// ==================== ФЛАГИ ====================
extern bool i2s_enabled;                                // Готов ли I2S?
extern bool sd_mounted;                                 // Готова ли SD-карта?
extern bool file_opened;                                // Открыт ли файл?
extern bool watchdog_enabled;                           // Включен ли сторожевой пес?