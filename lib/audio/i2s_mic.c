// i2s_mic.c
#include "i2s_mic.h"
#include "esp_log.h"

static const char *TAG = "I2S_MIC";

// ========== Глобальные переменные ==========
i2s_chan_handle_t rx_handle_master_left = NULL;
i2s_chan_handle_t rx_handle_master_right = NULL;
i2s_chan_handle_t rx_handle_slave_left = NULL;
i2s_chan_handle_t rx_handle_slave_right = NULL;

int32_t i2s_buffer_master_left[DMA_BUF_SIZE];
int32_t i2s_buffer_master_right[DMA_BUF_SIZE];
int32_t i2s_buffer_slave_left[DMA_BUF_SIZE];
int32_t i2s_buffer_slave_right[DMA_BUF_SIZE];

bool i2s_enabled = false;

// ========== Вспомогательная функция ==========
static esp_err_t i2s_init_channel(
    i2s_chan_handle_t *handle,
    i2s_port_t port_id,
    i2s_role_t role,
    gpio_num_t bclk_pin,
    gpio_num_t ws_pin,
    gpio_num_t din_pin
) {
    i2s_chan_config_t chan_cfg = {
        .id = port_id,
        .role = role,
        .dma_desc_num = 4,
        .dma_frame_num = DMA_BUF_SIZE,
        .auto_clear = true,
    };
    
    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed on port %d: %s", port_id, esp_err_to_name(ret));
        return ret;
    }
    
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT,
            I2S_SLOT_MODE_MONO
        ),
        .gpio_cfg = {
            .bclk = bclk_pin,
            .ws = ws_pin,
            .din = din_pin,
            .dout = I2S_GPIO_UNUSED,
            .mclk = I2S_GPIO_UNUSED,
        },
    };
    
    ret = i2s_channel_init_std_mode(*handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %s", esp_err_to_name(ret));
        i2s_del_channel(*handle);
        return ret;
    }
    
    ret = i2s_channel_enable(*handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(ret));
        i2s_del_channel(*handle);
        return ret;
    }
    
    return ESP_OK;
}

// ========== Инициализация ==========
esp_err_t i2s_init(void) {
    if (i2s_enabled) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing I2S for 4 ICS-43434 microphones...");
    
    // === MASTER PORT (I2S_NUM_0) ===
    esp_err_t ret = i2s_init_channel(&rx_handle_master_left, I2S_NUM_0, I2S_ROLE_MASTER,
                                      I2S_BCLK_MASTER_PIN, I2S_WS_MASTER_PIN, I2S_DIN_MASTER_LEFT);
    if (ret != ESP_OK) return ret;
    
    ret = i2s_init_channel(&rx_handle_master_right, I2S_NUM_0, I2S_ROLE_MASTER,
                           I2S_BCLK_MASTER_PIN, I2S_WS_MASTER_PIN, I2S_DIN_MASTER_RIGHT);
    if (ret != ESP_OK) return ret;
    
    // === SLAVE PORT (I2S_NUM_1) ===
    ret = i2s_init_channel(&rx_handle_slave_left, I2S_NUM_1, I2S_ROLE_SLAVE,
                           I2S_BCLK_SLAVE_PIN, I2S_WS_SLAVE_PIN, I2S_DIN_SLAVE_LEFT);
    if (ret != ESP_OK) return ret;
    
    ret = i2s_init_channel(&rx_handle_slave_right, I2S_NUM_1, I2S_ROLE_SLAVE,
                           I2S_BCLK_SLAVE_PIN, I2S_WS_SLAVE_PIN, I2S_DIN_SLAVE_RIGHT);
    if (ret != ESP_OK) return ret;
    
    i2s_enabled = true;
    ESP_LOGI(TAG, "All 4 I2S channels initialized successfully");
    return ESP_OK;
}

// ========== Деинициализация ==========
esp_err_t i2s_disable(void) {
    if (!i2s_enabled) return ESP_OK;
    
    i2s_channel_disable(rx_handle_master_left);
    i2s_channel_disable(rx_handle_master_right);
    i2s_channel_disable(rx_handle_slave_left);
    i2s_channel_disable(rx_handle_slave_right);
    
    i2s_del_channel(rx_handle_master_left);
    i2s_del_channel(rx_handle_master_right);
    i2s_del_channel(rx_handle_slave_left);
    i2s_del_channel(rx_handle_slave_right);
    
    rx_handle_master_left = NULL;
    rx_handle_master_right = NULL;
    rx_handle_slave_left = NULL;
    rx_handle_slave_right = NULL;
    
    i2s_enabled = false;
    ESP_LOGI(TAG, "I2S disabled");
    return ESP_OK;
}

// ========== Чтение всех 4 каналов ==========
esp_err_t microphone_read_all_4ch(size_t *samples_read) {
    if (!i2s_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    
    size_t bytes_ch1 = 0, bytes_ch2 = 0, bytes_ch3 = 0, bytes_ch4 = 0;
    
    esp_err_t ret1 = i2s_channel_read(rx_handle_master_left, i2s_buffer_master_left,
                                      DMA_BUF_SIZE * 4, &bytes_ch1, portMAX_DELAY);
    esp_err_t ret2 = i2s_channel_read(rx_handle_master_right, i2s_buffer_master_right,
                                      DMA_BUF_SIZE * 4, &bytes_ch2, portMAX_DELAY);
    esp_err_t ret3 = i2s_channel_read(rx_handle_slave_left, i2s_buffer_slave_left,
                                      DMA_BUF_SIZE * 4, &bytes_ch3, portMAX_DELAY);
    esp_err_t ret4 = i2s_channel_read(rx_handle_slave_right, i2s_buffer_slave_right,
                                      DMA_BUF_SIZE * 4, &bytes_ch4, portMAX_DELAY);
    
    if (ret1 != ESP_OK || ret2 != ESP_OK || ret3 != ESP_OK || ret4 != ESP_OK) {
        return ESP_FAIL;
    }
    
    size_t s1 = bytes_ch1 / 4;
    size_t s2 = bytes_ch2 / 4;
    size_t s3 = bytes_ch3 / 4;
    size_t s4 = bytes_ch4 / 4;
    
    if (s1 != s2 || s1 != s3 || s1 != s4) {
        ESP_LOGW(TAG, "Sample mismatch: %d/%d/%d/%d", s1, s2, s3, s4);
    }
    
    *samples_read = s1;
    return ESP_OK;
}
