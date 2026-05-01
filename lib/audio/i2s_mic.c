#include "i2s_mic.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "I2S_MIC";

// Дескрипторы каналов
i2s_chan_handle_t rx_handle_master_left  = NULL;
i2s_chan_handle_t rx_handle_master_right = NULL;
i2s_chan_handle_t rx_handle_slave_left   = NULL;
i2s_chan_handle_t rx_handle_slave_right  = NULL;

// DMA-буферы на сэмплы
static int32_t dma_master_left [DMA_BUF_SIZE];
static int32_t dma_master_right[DMA_BUF_SIZE];
static int32_t dma_slave_left  [DMA_BUF_SIZE];
static int32_t dma_slave_right [DMA_BUF_SIZE];

// Потоковый буфер (для непрерывной записи)
stream_buffer_t stream_buf;
sync_block_buffer_t block_buf;

static TaskHandle_t reader_task = NULL;
static volatile bool reader_running = false;

// ----- Инициализация одного канала -----
static esp_err_t i2s_init_channel(i2s_chan_handle_t *handle, i2s_port_t port,
                                  i2s_role_t role, gpio_num_t bclk, gpio_num_t ws,
                                  gpio_num_t din) {
    i2s_chan_config_t chan_cfg = {
        .id = port,
        .role = role,
        .dma_desc_num = 4,
        .dma_frame_num = DMA_BUF_SIZE,
        .auto_clear = true,
    };
    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, handle);
    if (ret != ESP_OK) return ret;

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                         I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .bclk = bclk,
            .ws   = ws,
            .din  = din,
            .dout = I2S_GPIO_UNUSED,
            .mclk = I2S_GPIO_UNUSED,
        },
    };
    ret = i2s_channel_init_std_mode(*handle, &std_cfg);
    if (ret != ESP_OK) return ret;
    return i2s_channel_enable(*handle);
}

// ----- Инициализация всей подсистемы -----
esp_err_t i2s_init(void) {
    // Каналы с исправленными ролями
    esp_err_t ret;
    ret = i2s_init_channel(&rx_handle_master_left, I2S_NUM_0, I2S_ROLE_MASTER,
                           I2S_BCLK_MASTER_PIN, I2S_WS_MASTER_PIN, I2S_DIN_MASTER_LEFT);
    if (ret) return ret;
    ret = i2s_init_channel(&rx_handle_master_right, I2S_NUM_0, I2S_ROLE_SLAVE,
                           I2S_BCLK_MASTER_PIN, I2S_WS_MASTER_PIN, I2S_DIN_MASTER_RIGHT);
    if (ret) return ret;
    ret = i2s_init_channel(&rx_handle_slave_left, I2S_NUM_1, I2S_ROLE_SLAVE,
                           I2S_BCLK_SLAVE_PIN, I2S_WS_SLAVE_PIN, I2S_DIN_SLAVE_LEFT);
    if (ret) return ret;
    ret = i2s_init_channel(&rx_handle_slave_right, I2S_NUM_1, I2S_ROLE_SLAVE,
                           I2S_BCLK_SLAVE_PIN, I2S_WS_SLAVE_PIN, I2S_DIN_SLAVE_RIGHT);
    if (ret) return ret;

    // Выделяем память для потокового буфера
    stream_buf.buffer = heap_caps_malloc(4 * STREAM_BUF_SIZE * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!stream_buf.buffer) return ESP_ERR_NO_MEM;
    stream_buf.write_idx = 0;
    stream_buf.read_idx  = 0;
    stream_buf.available = 0;
    stream_buf.mutex     = xSemaphoreCreateMutex();
    stream_buf.data_sem  = xSemaphoreCreateCounting(STREAM_BUF_SIZE, 0);

    // Выделяем память для блоков локализации
    for (int ch = 0; ch < 4; ch++) {
        for (int i = 0; i < SYNC_BLOCKS; i++) {
            block_buf.blocks[ch][i] = heap_caps_malloc(FFT_SIZE * sizeof(int16_t), MALLOC_CAP_SPIRAM);
            if (!block_buf.blocks[ch][i]) return ESP_ERR_NO_MEM;
        }
    }
    for (int i = 0; i < SYNC_BLOCKS; i++) block_buf.block_ready[i] = false;
    block_buf.read_idx = 0;
    block_buf.block_sem = xSemaphoreCreateCounting(SYNC_BLOCKS, 0);
    block_buf.mutex     = xSemaphoreCreateMutex();

    // Запускаем задачу-читатель
    reader_running = true;
    xTaskCreatePinnedToCore(i2s_reader_task, "i2s_reader", 4096, NULL,
                            I2S_READER_PRIORITY, &reader_task, APP_CPU_NUM);
    ESP_LOGI(TAG, "I2S subsystem started with stream + localization buffers");
    return ESP_OK;
}

// ----- Задача непрерывного сбора данных -----
static void i2s_reader_task(void *pv) {
    // Локальные накопительные буферы для набора FFT_SIZE сэмплов
    int16_t *loc_accum[4];
    for (int ch = 0; ch < 4; ch++)
        loc_accum[ch] = heap_caps_malloc(FFT_SIZE * sizeof(int16_t), MALLOC_CAP_INTERNAL);

    size_t accum_cnt = 0;
    int32_t *dma_bufs[4] = {dma_master_left, dma_master_right,
                             dma_slave_left, dma_slave_right};
    i2s_chan_handle_t handles[4] = {rx_handle_master_left, rx_handle_master_right,
                                    rx_handle_slave_left, rx_handle_slave_right};

    while (reader_running) {
        size_t chunk = 0;
        // Читаем из всех каналов, пока не получим хотя бы один сэмпл в каждом
        for (int ch = 0; ch < 4; ch++) {
            size_t bytes_read;
            esp_err_t err = i2s_channel_read(handles[ch],
                                             dma_bufs[ch],
                                             DMA_BUF_SIZE * sizeof(int32_t),
                                             &bytes_read, portMAX_DELAY);
            if (err == ESP_OK) {
                size_t samples = bytes_read / sizeof(int32_t);
                if (chunk == 0) chunk = samples;
                else if (samples < chunk) chunk = samples; // берём минимум для синхронизации
            }
        }
        if (chunk == 0) continue;

        // Преобразуем и заносим в потоковый буфер и накопители локализации
        for (size_t i = 0; i < chunk; i++) {
            // Преобразование 32->16 бит
            int16_t vals[4];
            for (int ch = 0; ch < 4; ch++) {
                int32_t raw = dma_bufs[ch][i];
                vals[ch] = (int16_t)((raw >> 16) * GAIN_FACTOR);
            }
            // Потоковый буфер
            xSemaphoreTake(stream_buf.mutex, portMAX_DELAY);
            size_t w = stream_buf.write_idx;
            memcpy(&stream_buf.buffer[w * 4], vals, 4 * sizeof(int16_t));
            stream_buf.write_idx = (w + 1) % STREAM_BUF_SIZE;
            stream_buf.available++;
            xSemaphoreGive(stream_buf.data_sem);
            xSemaphoreGive(stream_buf.mutex);

            // Накопление для локализации
            for (int ch = 0; ch < 4; ch++)
                loc_accum[ch][accum_cnt] = vals[ch];
            accum_cnt++;
            if (accum_cnt == FFT_SIZE) {
                // Копируем в свободный блок
                xSemaphoreTake(block_buf.mutex, portMAX_DELAY);
                int free_slot = -1;
                for (int s = 0; s < SYNC_BLOCKS; s++) {
                    if (!block_buf.block_ready[s]) { free_slot = s; break; }
                }
                if (free_slot >= 0) {
                    for (int ch = 0; ch < 4; ch++)
                        memcpy(block_buf.blocks[ch][free_slot], loc_accum[ch],
                               FFT_SIZE * sizeof(int16_t));
                    block_buf.block_ready[free_slot] = true;
                    xSemaphoreGive(block_buf.block_sem);
                } else {
                    ESP_LOGW(TAG, "No free localization block – discarding");
                }
                xSemaphoreGive(block_buf.mutex);
                accum_cnt = 0;
            }
        }
    }
    // очистка
    for (int ch = 0; ch < 4; ch++) free(loc_accum[ch]);
    vTaskDelete(NULL);
}

// ----- Интерфейс для локализации -----
esp_err_t i2s_get_localization_block(int16_t **ch1, int16_t **ch2,
                                     int16_t **ch3, int16_t **ch4) {
    if (xSemaphoreTake(block_buf.block_sem, portMAX_DELAY) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    xSemaphoreTake(block_buf.mutex, portMAX_DELAY);
    // Находим первый готовый блок
    while (!block_buf.block_ready[block_buf.read_idx]) {
        block_buf.read_idx = (block_buf.read_idx + 1) % SYNC_BLOCKS;
    }
    int idx = block_buf.read_idx;
    *ch1 = block_buf.blocks[0][idx];
    *ch2 = block_buf.blocks[1][idx];
    *ch3 = block_buf.blocks[2][idx];
    *ch4 = block_buf.blocks[3][idx];
    xSemaphoreGive(block_buf.mutex);
    return ESP_OK;
}

void i2s_release_localization_block(void) {
    xSemaphoreTake(block_buf.mutex, portMAX_DELAY);
    block_buf.block_ready[block_buf.read_idx] = false;
    block_buf.read_idx = (block_buf.read_idx + 1) % SYNC_BLOCKS;
    xSemaphoreGive(block_buf.mutex);
}

// ----- Интерфейс для непрерывной записи на SD -----
esp_err_t i2s_read_continuous(int16_t *dest, size_t count) {
    size_t collected = 0;
    while (collected < count) {
        // Ожидаем хотя бы один сэмпл
        if (xSemaphoreTake(stream_buf.data_sem, pdMS_TO_TICKS(100)) != pdTRUE)
            return ESP_ERR_TIMEOUT;

        xSemaphoreTake(stream_buf.mutex, portMAX_DELAY);
        size_t available = stream_buf.available;
        size_t to_copy = (available < (count - collected)) ? available : (count - collected);
        // Копируем от read_idx линейно, учитывая кольцо
        for (size_t i = 0; i < to_copy; i++) {
            size_t idx = (stream_buf.read_idx + i) % STREAM_BUF_SIZE;
            memcpy(&dest[(collected + i) * 4], &stream_buf.buffer[idx * 4], 4 * sizeof(int16_t));
        }
        stream_buf.read_idx = (stream_buf.read_idx + to_copy) % STREAM_BUF_SIZE;
        stream_buf.available -= to_copy;
        collected += to_copy;
        // Обновляем семафор (уменьшаем на скопированное количество)
        for (size_t i = 0; i < to_copy; i++) {
            // Уже взяли один семафор выше, поэтому компенсируем:
            if (i > 0) xSemaphoreTake(stream_buf.data_sem, 0);
        }
        xSemaphoreGive(stream_buf.mutex);
    }
    return ESP_OK;
}

// ----- Деинициализация -----
esp_err_t i2s_disable(void) {
    reader_running = false;
    if (reader_task) {
        vTaskDelay(pdMS_TO_TICKS(50));
        vTaskDelete(reader_task);
    }
    if (rx_handle_master_left)  { i2s_channel_disable(rx_handle_master_left); i2s_del_channel(rx_handle_master_left); }
    if (rx_handle_master_right) { i2s_channel_disable(rx_handle_master_right); i2s_del_channel(rx_handle_master_right); }
    if (rx_handle_slave_left)   { i2s_channel_disable(rx_handle_slave_left); i2s_del_channel(rx_handle_slave_left); }
    if (rx_handle_slave_right)  { i2s_channel_disable(rx_handle_slave_right); i2s_del_channel(rx_handle_slave_right); }
    // освобождаем память буферов (по необходимости)
    return ESP_OK;
}
