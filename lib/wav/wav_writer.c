#include "wav_writer.h"

static const char *TAG = "WAV";

// Генерация имени файла в формате 000001.wav
static void generate_filename(char *filename, size_t len, uint32_t number) {
    snprintf(filename, len, "%s/%06" PRIu32 ".wav", AUDIO_DIR_PATH, number);
}

esp_err_t create_wav(void) {
    // Если файл уже открыт, сначала закрываем
    if (file_opened) {
        ESP_LOGW(TAG, "WAV file already opened, closing first");
        close_wav(0);
    }
    
    // Убеждаемся, что предыдущий указатель обнулен
    if (f != NULL) {
        fclose(f);
        f = NULL;
    }
    
    // Инициализируем счетчик (если еще не инициализирован)
    static bool counter_initialized = false;
    if (!counter_initialized) {
        if (counter_init() != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize counter");
            return ESP_FAIL;
        }
        counter_initialized = true;
    }
    
    // Получаем следующий номер файла
    current_file_number = counter_get_current() + 1;
    
    // Генерируем имя файла
    generate_filename(current_filename, sizeof(current_filename), current_file_number);
    
    // Создаем файл
    f = fopen(current_filename, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to create file: %s", current_filename);
        ESP_LOGE(TAG, "errno: %d (%s)", errno, strerror(errno));
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Created file: %s (#%06" PRIu32 ")", current_filename, current_file_number);
    
    // Пишем временный заголовок
    memset(wav_header, 0, sizeof(wav_header));
    size_t written = fwrite(wav_header, 1, 44, f);
    if (written != 44) {
        ESP_LOGE(TAG, "Failed to write WAV header");
        fclose(f);
        f = NULL;
        return ESP_FAIL;
    }
    
    fflush(f);
    file_opened = true;
    
    return ESP_OK;
}

esp_err_t write_wav(size_t buffer_size) {
    if (!file_opened || f == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    size_t bytes_to_write = buffer_size * sizeof(int16_t);
    size_t written = fwrite(pcm_buffer, 1, bytes_to_write, f);
    
    if (written != bytes_to_write) {
        ESP_LOGE(TAG, "Failed to write PCM data: wrote %d/%d bytes", 
                 written, bytes_to_write);
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t close_wav(uint32_t total_samples) {
    if (!file_opened || f == NULL) {
        ESP_LOGW(TAG, "No open WAV file to close");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Closing WAV file: %s", current_filename);
    
    if (total_samples > 0) {
        // Обновляем WAV заголовок
        uint32_t data_size = total_samples * BYTES_PER_SAMPLE;
        uint32_t file_size = data_size + 36;
        
        // RIFF header
        wav_header[0] = 'R'; wav_header[1] = 'I'; wav_header[2] = 'F'; wav_header[3] = 'F';
        wav_header[4] = file_size & 0xFF;
        wav_header[5] = (file_size >> 8) & 0xFF;
        wav_header[6] = (file_size >> 16) & 0xFF;
        wav_header[7] = (file_size >> 24) & 0xFF;
        
        wav_header[8] = 'W'; wav_header[9] = 'A'; wav_header[10] = 'V'; wav_header[11] = 'E';
        
        // fmt subchunk
        wav_header[12] = 'f'; wav_header[13] = 'm'; wav_header[14] = 't'; wav_header[15] = ' ';
        wav_header[16] = 16; wav_header[17] = 0; wav_header[18] = 0; wav_header[19] = 0;
        wav_header[20] = 1; wav_header[21] = 0;
        wav_header[22] = 1; wav_header[23] = 0;
        wav_header[24] = SAMPLE_RATE & 0xFF;
        wav_header[25] = (SAMPLE_RATE >> 8) & 0xFF;
        wav_header[26] = (SAMPLE_RATE >> 16) & 0xFF;
        wav_header[27] = (SAMPLE_RATE >> 24) & 0xFF;
        wav_header[28] = BYTE_RATE & 0xFF;
        wav_header[29] = (BYTE_RATE >> 8) & 0xFF;
        wav_header[30] = (BYTE_RATE >> 16) & 0xFF;
        wav_header[31] = (BYTE_RATE >> 24) & 0xFF;
        wav_header[32] = 2; wav_header[33] = 0;
        wav_header[34] = 16; wav_header[35] = 0;
        
        // data subchunk
        wav_header[36] = 'd'; wav_header[37] = 'a'; wav_header[38] = 't'; wav_header[39] = 'a';
        wav_header[40] = data_size & 0xFF;
        wav_header[41] = (data_size >> 8) & 0xFF;
        wav_header[42] = (data_size >> 16) & 0xFF;
        wav_header[43] = (data_size >> 24) & 0xFF;
        
        // Обновляем заголовок в файле
        fseek(f, 0, SEEK_SET);
        fwrite(wav_header, 1, 44, f);
        fflush(f);
        
        // Закрываем файл
        fclose(f);
        f = NULL;
        file_opened = false;
        
        // Инкрементируем счетчик ТОЛЬКО после успешной записи
        if (counter_increment() != ESP_OK) {
            ESP_LOGW(TAG, "Failed to increment counter, but file saved");
        }
        
        ESP_LOGI(TAG, "File saved: %s", current_filename);
        ESP_LOGI(TAG, "File size: %" PRIu32 " bytes", file_size + 8);
        ESP_LOGI(TAG, "Duration: %.2f seconds", (float)total_samples / SAMPLE_RATE);
        
    } else {
        // Закрываем файл
        fclose(f);
        f = NULL;
        file_opened = false;

        // Если нет данных, удаляем пустой файл
        ESP_LOGI(TAG, "No data recorded, deleting empty file");
        remove(current_filename);
        ESP_LOGI(TAG, "Empty file deleted");
    }
    
    return ESP_OK;
}

esp_err_t wav_deinit(void) {
    if (file_opened) {
        close_wav(0);
    }
    
    if (f != NULL) {
        fclose(f);
        f = NULL;
    }
    
    file_opened = false;
    memset(wav_header, 0, sizeof(wav_header));
    memset(current_filename, 0, sizeof(current_filename));
    
    return ESP_OK;
}

uint32_t get_current_file_number(void) {
    return current_file_number;
}

const char* get_current_filename(void) {
    return current_filename;
}