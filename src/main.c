#include "i2s_mic.h"
#include "cnn_wrapper.h"
#include "sd_card_manager.h"
#include "wav_writer.h"
#include "watchdog_timer.h"
#include "led_manager.h"
#include "localization.h"
#include "app_config.h"



// Тег для системы логирования
static const char *TAG = "MAIN";

TaskHandle_t init_handle = NULL;            // Задача инициализации интерфейсов
TaskHandle_t deinit_handle = NULL;          // Задача деинициализации интерфейсов
TaskHandle_t recording_handle = NULL;       // Задача записи звука
TaskHandle_t localization_handle = NULL;    // Задача локализации звука
QueueHandle_t azimuth_queue;

// Определения переменных из app_config.h
i2s_chan_handle_t rx_handle = NULL;
sdmmc_card_t *card = NULL;
FILE *f = NULL;
uint32_t current_counter = 0;
uint8_t wav_header[44] = {0};
char current_filename[64] = {0};
uint32_t current_file_number = 0;
bool sd_mounted = false;
bool file_opened = false;
bool watchdog_enabled = true;

// Системные переменные
static volatile bool system_enabled = false;
static volatile bool recording_started = false;
static volatile bool deinit_in_progress = false;
static volatile uint32_t total_samples = 0;

// Семафоры
SemaphoreHandle_t resource_mut = NULL;  //  Определяет доступ к глобальным переменным для только одной задачи - Нужен для избежание гонки за ресурсы у задач
SemaphoreHandle_t system_sem = NULL;    //  Семафор включения - оживает при нажатии на кнопку -> система инициализируется/деинициализируется
SemaphoreHandle_t record_sem = NULL;    //  Семафор записи - оживает при нажатии на кнопку -> начинается/заканчивается запись
SemaphoreHandle_t i2s_stop_sem = NULL;  //  Семафор для остановки I2S

// ISR обработчик для кнопки GPIO 19
static void IRAM_ATTR gpio_system_isr_handler(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    // Отдаем семафор
    xSemaphoreGiveFromISR(system_sem, &xHigherPriorityTaskWoken);
    
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// ISR обработчик для кнопки GPIO 20
static void IRAM_ATTR gpio_record_isr_handler(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    // Отдаем семафор
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
            
            if (localization_init() == ESP_OK) {
    xTaskCreatePinnedToCore(localization_task, "localization", 4096, NULL,
                            LOCALIZATION_PRIORITY, &localization_handle, APP_CPU_NUM);
}
            
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

// ----- Задача локализации  -----
void localization_task(void *arg) {
    int16_t *ch1, *ch2, *ch3, *ch4;
    while (1) {
        if (i2s_get_localization_block(&ch1, &ch2, &ch3, &ch4) == ESP_OK) {
            float az = localization_calculate_azimuth(ch1, ch2, ch3, ch4);
            xQueueOverwrite(azimuth_queue, &az);
            i2s_release_localization_block();
            ESP_LOGI(TAG, "Azimuth: %.1f°", az);
        }
    }
}

// ========== НОВАЯ ЗАДАЧА ЗАПИСИ ==========
void recording(void* arg) {
    int16_t rec_buf[DMA_BUF_SIZE * 4];  // временный буфер
    while (1) {
        if (!recording_started) {
            if (xSemaphoreTake(record_sem, portMAX_DELAY) == pdTRUE) {
                if (system_enabled && !deinit_in_progress) {
                    if (xSemaphoreTake(resource_mut, portMAX_DELAY) == pdTRUE) {
                        if (create_wav() == ESP_OK) {
                            recording_started = true;
                            total_samples = 0;
                            led_green_on();
                            ESP_LOGI(TAG, "Recording started: #%06" PRIu32 ".wav", 
                                     get_current_file_number());
                        } else {
                            ESP_LOGE(TAG, "Failed to create WAV file");
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
                led_green_off();
                xSemaphoreGive(resource_mut);
            }
            continue;
        }
        
        if (deinit_in_progress || !system_enabled) {
            recording_started = false;
            continue;
        }
        
        // ===== ОСНОВНОЙ ЦИКЛ ЗАПИСИ (4 КАНАЛА) =====
        if (recording_started) {
    if (i2s_read_continuous(rec_buf, DMA_BUF_SIZE) == ESP_OK) {
        write_wav(rec_buf, DMA_BUF_SIZE);
        total_samples += DMA_BUF_SIZE;
    }
}
    }
}

void drone_test_task(void* arg) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    wrapper();
    vTaskDelete(NULL);
}

void app_main(void) {
    // Пока нейросеть запускается в тестовом формате, получая готовый звук из flash
    UBaseType_t uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "Main task stack free: %d bytes", uxHighWaterMark * 4);

    xTaskCreatePinnedToCore(drone_test_task, "drone_test", 16384, NULL, 5, NULL, CNN_CORE);
    
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

    // Без этой приблуды GPIO не будет работать
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

    // Запуск
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
    if (xTaskCreatePinnedToCore(init, "init", 4096, NULL, 10, &init_handle, MAIN_CORE) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create init task");
        return;
    }
    
    if (xTaskCreatePinnedToCore(deinit, "deinit", 4096, NULL, 10, &deinit_handle, MAIN_CORE) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create deinit task");
        return;
    }
    
    if (xTaskCreatePinnedToCore(recording, "recording", 8192, NULL, 11, &recording_handle, MAIN_CORE) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create recording task");
        return;
    }
    
    ESP_LOGI(TAG, "All tasks created successfully");
    ESP_LOGI(TAG, "System ready. Press button:");
    ESP_LOGI(TAG, "  - Short press (<%d ms): Toggle recording", LONG_PRESS);
    ESP_LOGI(TAG, "  - Long press (>=%d ms): Toggle system ON/OFF", LONG_PRESS);
}
