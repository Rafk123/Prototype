#include "file_counter.h"

static const char *TAG = "COUNTER";

static esp_err_t create_audio_dir(void) {
    struct stat st;
    
    if (stat("/sdcard", &st) != 0) {
        ESP_LOGE(TAG, "/sdcard not mounted");
        return ESP_ERR_NOT_FOUND;
    }
    
    if (stat(AUDIO_DIR_PATH, &st) != 0) {
        ESP_LOGI(TAG, "Creating audio directory: %s", AUDIO_DIR_PATH);
        
        if (mkdir(AUDIO_DIR_PATH, 0777) != 0) {
            ESP_LOGE(TAG, "Failed to create directory: %s", strerror(errno));
            return ESP_FAIL;
        }
        
        ESP_LOGI(TAG, "Directory created successfully");
    }
    
    return ESP_OK;
}

esp_err_t counter_init(void) {
    vTaskDelay(pdMS_TO_TICKS(100));
    
    esp_err_t ret = create_audio_dir();
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Пробуем открыть файл counter.txt
    FILE *f = fopen(COUNTER_FILE_PATH, "r");
    if (f == NULL) {
        // Файла нет - создаем с 0
        ESP_LOGI(TAG, "Counter file not found, creating with 0");
        
        f = fopen(COUNTER_FILE_PATH, "w");
        if (f == NULL) {
            ESP_LOGE(TAG, "Failed to create counter file: %s", strerror(errno));
            return ESP_FAIL;
        }
        
        fprintf(f, "0");
        fclose(f);
        
        current_counter = 0;
        ESP_LOGI(TAG, "Counter initialized: 0");
        return ESP_OK;
    }
    
    // Читаем существующий счетчик
    if (fscanf(f, "%" PRIu32 "", &current_counter) != 1) {
        ESP_LOGW(TAG, "Failed to read counter, resetting to 0");
        current_counter = 0;
    }
    
    fclose(f);
    ESP_LOGI(TAG, "Counter loaded: %" PRIu32 "", current_counter);
    
    return ESP_OK;
}

esp_err_t counter_increment(void) {
    current_counter++;
    
    FILE *f = fopen(COUNTER_FILE_PATH, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to update counter file: %s", strerror(errno));
        return ESP_FAIL;
    }
    
    fprintf(f, "%" PRIu32 "", current_counter);
    fclose(f);
    
    ESP_LOGI(TAG, "Counter incremented to: %" PRIu32 "", current_counter);
    return ESP_OK;
}

uint32_t counter_get_current(void) {
    return current_counter;
}

esp_err_t counter_reset(void) {
    current_counter = 0;
    return counter_increment();
}