#include "i2s_mic.h"

static const char *TAG = "I2S_MIC";
static esp_err_t ret;

esp_err_t i2s_init() {

    // ========== Инициализация I2S ==========

    if (i2s_enabled) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing I2S...");

    // Конфигурация канала для DMA
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,                    // Номер контроллера I2S
        .role = I2S_ROLE_MASTER,            // Режим работы в рамках интерфейса
        .dma_desc_num = 4,                  // Количество DMA дескрипторов
        .dma_frame_num = DMA_BUF_SIZE,      // Размер DMA буфера
        .auto_clear = true,                 // 0 в буфере, если "тишина"
    };

    // Конфигурация режима работы (провода и сигнал)
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),     // Тактирование по SAMPLE_RATE
        .slot_cfg = {                                           // Конфигурация сигнала
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,         // Размер сэмпла
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT,         // Ширина посадочного места на шине
            .slot_mode = I2S_SLOT_MODE_STEREO,                  // Режим работы шины
            .slot_mask = I2S_STD_SLOT_LEFT,                     // Читаем только левый канал!
            .ws_width = 32,                                     // Длина импульса выбора канала
            .ws_pol = false,                                    // Полярность сигнала выбора канала (WS/LRCLK)
            .bit_shift = true,                                  // Стандарт протокола I2S Phillips
        },
        .gpio_cfg = {                                           // Конфигурация пинов
            .bclk = I2S_BCK,                                    // Ножка тактового сигнала
            .ws = I2S_WS,                                       // Выбор канала для записи в буфер (левый/правый)
            .din = I2S_DIN,                                     // Пин для входящих данных (От микрофона)
            .dout = I2S_GPIO_UNUSED,                            // Пин для исходящих данных (на динамик)
            .mclk = I2S_GPIO_UNUSED,                            // Основной системный такт
            .invert_flags = {                                   // Полярность сигналов на пинах
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    // Создаем новый программный канал I2S
    // Задаем три параметра: конфигурацию, хендл_отправки, хендл_приема
    ret = i2s_new_channel(&chan_cfg, NULL, &rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Инициализация режима работы для нашего хендла
    ret = i2s_channel_init_std_mode(rx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %s", esp_err_to_name(ret));
        i2s_del_channel(rx_handle);
        return ret;
    }

    // Включение уже настроенного канала
    ret = i2s_channel_enable(rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(ret));
        i2s_del_channel(rx_handle);
        return ret;
    }

    ESP_LOGI(TAG, "I2S initialized successfully");
    i2s_enabled = true;
    return ESP_OK;
    
}

esp_err_t i2s_disable() {
    
    if (!i2s_enabled) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "I2S shutdown...");
    i2s_channel_disable(rx_handle);
    i2s_del_channel(rx_handle);
    ESP_LOGI(TAG, "I2S disabled");
    i2s_enabled = false;

    return ESP_OK;

}

esp_err_t microphone_read(size_t* bytes_read) {

    ret = i2s_channel_read(rx_handle, i2s_buffer, sizeof(i2s_buffer), bytes_read, portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_reading failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;

}

esp_err_t microphone_read_with_timeout(size_t *bytes_read, TickType_t timeout) {
    if (!rx_handle || !i2s_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Используем таймаут вместо portMAX_DELAY
    esp_err_t ret = i2s_channel_read(rx_handle, i2s_buffer, DMA_BUF_SIZE * 4, bytes_read, timeout);
    return ret;
}
