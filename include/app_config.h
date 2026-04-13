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
#define SD_CS   GPIO_NUM_10         // Chip Select  - выбор esp устройства периферии
#define SD_MOSI GPIO_NUM_11         // Slave In     - линия передачи данных из esp в sd-card
#define SD_MISO GPIO_NUM_13         // Slave Out    - линия передачи данных из sd-card в esp
#define SD_CLK  GPIO_NUM_12         // Clock        - сигнал синхронизации

#define I2S_BCK GPIO_NUM_4          // Bit Clock    - сигнал синхронизации
#define I2S_WS  GPIO_NUM_5          // Word Select  - сигнал для выбора канала
#define I2S_DIN GPIO_NUM_6          // Data Out     - сигнал цифрового звука

#define SYSTEM_BUTTON GPIO_NUM_19   // Кнопка для включения и выключения системы
#define RECORD_BUTTON GPIO_NUM_20   // Кнопка для включения и выключения записи аудио

#define LED_RED_PIN     GPIO_NUM_17 // Красный LED - индикация системы
#define LED_GREEN_PIN   GPIO_NUM_16 // Зеленый LED - индикация записи

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
#define BYTE_RATE (SAMPLE_RATE * BYTES_PER_SAMPLE)      // 

// ==================== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ====================
extern i2s_chan_handle_t rx_handle;                     // Хендл для программного I2S канала    
extern sdmmc_card_t *card;                              // Указатель на SD-карту
extern FILE *f;                                         // Указатель на файл
extern uint32_t current_counter;                        // Системный счетчик аудиофайлов
extern uint8_t wav_header[44];                          // Заголовок WAV-файла
extern int32_t i2s_buffer[DMA_BUF_SIZE];                // Сырые данные из i2s dma-буфера
extern int16_t pcm_buffer[DMA_BUF_SIZE];                // Обработанные данные из i2s dma-буфера
extern char current_filename[64];                       // Текущее название аудиофайла
extern uint32_t current_file_number;                    // Нынешний номер записывваемого аудиофайла

// ==================== ФЛАГИ ====================
extern bool i2s_enabled;                                // Готов ли I2S?
extern bool sd_mounted;                                 // Готова ли SD-карта?
extern bool file_opened;                                // Открыт ли файл?
extern bool watchdog_enabled;                           // Включен ли сторожевой пес?