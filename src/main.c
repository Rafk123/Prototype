#include "i2s_mic.h"
#include "sd_card_manager.h"
#include "wav_writer.h"
#include "watchdog_timer.h"
#include "led_manager.h"
#include "app_config.h"

static const char *TAG = "MAIN";

TaskHandle_t init_handle = NULL;
TaskHandle_t deinit_handle = NULL;
TaskHandle_t recording_handle = NULL;

// Определения переменных из app_config.h
i2s_chan_handle_t rx_handle = NULL;
sdmmc_card_t *card = NULL;
FILE *f = NULL;
uint32_t current_counter = 0;
uint8_t wav_header[44] = {0};
char current_filename[64] = {0};
uint32_t current_file_number = 0;
bool i2s_enabled = false;
bool sd_mounted = false;
bool file_opened = false;
bool watchdog_enabled = true;

// Буферы
int32_t i2s_buffer[DMA_BUF_SIZE];
int16_t pcm_buffer[DMA_BUF_SIZE];

// Системные переменные
static volatile bool system_enabled = false;
static volatile bool recording_started = false;
static volatile bool deinit_in_progress = false;
static volatile uint32_t total_samples = 0;

// Семафоры
SemaphoreHandle_t resource_mut = NULL;
SemaphoreHandle_t system_sem = NULL;
SemaphoreHandle_t record_sem = NULL;
SemaphoreHandle_t i2s_stop_sem = NULL;  // Добавлен семафор для остановки I2S

// ISR обработчик для кнопки GPIO 19
static void IRAM_ATTR gpio_system_isr_handler(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    xSemaphoreGiveFromISR(system_sem, &xHigherPriorityTaskWoken);
    
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// ISR обработчик для кнопки GPIO 20
static void IRAM_ATTR gpio_record_isr_handler(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    xSemaphoreGiveFromISR(record_sem, &xHigherPriorityTaskWoken);
    
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// Задача инициализации
void init(void* arg) {
    while (true) {
        xSemaphoreTake(system_sem, portMAX_DELAY);
        
        if (!system_enabled && xSemaphoreTake(resource_mut, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "=== SYSTEM START ===");
            
            // Красный LED горит - система включается
            led_red_on();
            
            // 1. Сначала инициализируем I2S
            if (i2s_init() != ESP_OK) {
                ESP_LOGE(TAG, "I2S init failed");
                led_red_off();  // Ошибка - красный гаснет
                i2s_disable();
                xSemaphoreGive(resource_mut);
                continue;
            }
            
            // 2. Инициализируем SD карту
            if (sd_init() != ESP_OK) {
                ESP_LOGE(TAG, "SD init failed");
                led_red_off();  // Ошибка - красный гаснет
                i2s_disable();
                sd_disable();
                xSemaphoreGive(resource_mut);
                continue;
            }
            
            // 3. Даем время на стабилизацию файловой системы
            vTaskDelay(pdMS_TO_TICKS(100));
            
            system_enabled = true;
            ESP_LOGI(TAG, "System started successfully");
            
            // Красный LED продолжает гореть - система активна
            
            xSemaphoreGive(resource_mut);
        }
    }
}

// Задача деинициализации
void deinit(void* arg) {
    while (true) {
        xSemaphoreTake(system_sem, portMAX_DELAY);
        
        if (system_enabled && xSemaphoreTake(resource_mut, portMAX_DELAY) == pdTRUE) {
            
            // Если идет запись - просто игнорируем
            if (recording_started) {
                ESP_LOGW(TAG, "Can't deinit while recording (file %06" PRIu32 ".wav active)",
                         get_current_file_number());
                ESP_LOGI(TAG, "Press long press again after recording stops");
                
                xSemaphoreGive(resource_mut);
                continue;
            }
            
            // Выключение
            ESP_LOGI(TAG, "=== SYSTEM STOP ===");
            
            // Выключаем оба LED
            led_red_off();
            led_green_off();
            
            i2s_disable();
            sd_disable();
            system_enabled = false;
            ESP_LOGI(TAG, "=== SYSTEM STOPPED ===");
            
            xSemaphoreGive(resource_mut);
        }
    }
}

// Задача записи
void recording(void* arg) {
    while (true) {
        
        if (!recording_started) {
            if (xSemaphoreTake(record_sem, portMAX_DELAY) == pdTRUE) {
                if (system_enabled && !deinit_in_progress) {
                    if (xSemaphoreTake(resource_mut, portMAX_DELAY) == pdTRUE) {
                        if (create_wav() == ESP_OK) {
                            recording_started = true;
                            total_samples = 0;
                            
                            // Зеленый LED горит - идет запись
                            led_green_on();
                            
                            ESP_LOGI(TAG, "Recording started: #%06" PRIu32 ".wav", 
                                     get_current_file_number());
                        } else {
                            ESP_LOGE(TAG, "Failed to create WAV file");
                            // Ошибка - выключаем красный (система не готова)
                            led_red_off();
                        }
                        xSemaphoreGive(resource_mut);
                    }
                }
            }
            continue;
        }
        
        // Остановка записи
        if (xSemaphoreTake(record_sem, 0) == pdTRUE) {
            if (xSemaphoreTake(resource_mut, portMAX_DELAY) == pdTRUE) {
                ESP_LOGI(TAG, "Recording stopped");
                close_wav(total_samples);
                recording_started = false;
                total_samples = 0;
                
                // Зеленый гаснет, красный продолжает гореть
                led_green_off();
                
                xSemaphoreGive(resource_mut);
            }
            continue;
        }
        
        if (deinit_in_progress || !system_enabled) {
            recording_started = false;
            continue;
        }
        
        // Запись данных
        size_t bytes_read = 0;
        esp_err_t ret = microphone_read(&bytes_read);
        
        if (ret == ESP_OK && bytes_read > 0) {
            int samples = bytes_read / 4;
            
            for (int j = 0; j < samples; ++j) {
                int32_t sample = (i2s_buffer[j] >> 16) * GAIN_FACTOR;
                if (sample > INT16_MAX) sample = INT16_MAX;
                if (sample < INT16_MIN) sample = INT16_MIN;
                pcm_buffer[j] = (int16_t)sample;
            }
            
            write_wav(samples);
            total_samples += samples;
        }
    }
}

void app_main(void) {
    
    // Выключаем, чтобы не было головной боли
    watchdog_deinit();

    // Инициализация LED
    led_init();
    
    // Красный LED выключен (система не активна)
    led_red_off();
    led_green_off();
    
    // Создаем семафоры
    resource_mut = xSemaphoreCreateMutex();
    if (!resource_mut) {
        ESP_LOGE(TAG, "Failed to create resource mutex");
        return;
    }
    
    record_sem = xSemaphoreCreateBinary();
    if (!record_sem) {
        ESP_LOGE(TAG, "Failed to create start recording semaphore");
        return;
    }
    
    system_sem = xSemaphoreCreateBinary();
    if (!system_sem) {
        ESP_LOGE(TAG, "Failed to create toggle system semaphore");
        return;
    }
    
    i2s_stop_sem = xSemaphoreCreateBinary();
    if (!i2s_stop_sem) {
        ESP_LOGE(TAG, "Failed to create I2S stop semaphore");
        return;
    }
    
    ESP_LOGI(TAG, "All synchronization primitives created");

    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    
    // Настройка кнопок (Pull-up, так как замыкают на землю)
    gpio_config_t btn_config = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE, // Реагируем на отпускание? Или на нажатие?
        // Лучше на нажатие (FALLING), но т.к. кнопка дает 0 при нажатии:
        // .intr_type = GPIO_INTR_NEGEDGE 
        // Я рекомендую использовать GPIO_INTR_ANYEDGE, если нет дребезга, но лучше NEGEDGE
    };

    gpio_set_drive_capability(SYSTEM_BUTTON, GPIO_DRIVE_CAP_0);
    gpio_set_drive_capability(RECORD_BUTTON, GPIO_DRIVE_CAP_0);
    
    // Настройка пина 19
    btn_config.pin_bit_mask = 1ULL << SYSTEM_BUTTON;
    gpio_config(&btn_config);
    gpio_isr_handler_add(SYSTEM_BUTTON, gpio_system_isr_handler, NULL);
    
    // Настройка пина 20
    btn_config.pin_bit_mask = 1ULL << RECORD_BUTTON;
    gpio_config(&btn_config);
    gpio_isr_handler_add(RECORD_BUTTON, gpio_record_isr_handler, NULL);

    // Включаем прерывания
    gpio_intr_enable(SYSTEM_BUTTON);
    gpio_intr_enable(RECORD_BUTTON);
    
    // Создание задач
    if (xTaskCreate(init, "init", 4096, NULL, 10, &init_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create init task");
        return;
    }
    
    if (xTaskCreate(deinit, "deinit", 4096, NULL, 10, &deinit_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create deinit task");
        return;
    }
    
    if (xTaskCreate(recording, "recording", 8192, NULL, 11, &recording_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create recording task");
        return;
    }
    
    ESP_LOGI(TAG, "All tasks created successfully");
    ESP_LOGI(TAG, "System ready. Press button:");
    ESP_LOGI(TAG, "  - Short press (<%d ms): Toggle recording", LONG_PRESS);
    ESP_LOGI(TAG, "  - Long press (>=%d ms): Toggle system ON/OFF", LONG_PRESS);
}