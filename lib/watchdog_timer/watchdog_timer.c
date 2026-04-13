#include <watchdog_timer.h>

static const char *TAG = "WATCHDOG";
static esp_err_t ret;

esp_err_t watchdog_init() {

    if (watchdog_enabled) {
        return ESP_OK;
    }
    // ========== Конфигурация watchdog_timer ==========
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = 30000,                    // 30 секунд в миллисекундах
        .idle_core_mask = (1 << 0) | (1 << 1),  // Мониторить оба ядра
        .trigger_panic = false,                 // Не паниковать при срабатывании
    };
    ret = esp_task_wdt_init(&twdt_config);      // Инициализация сторожевого таймера
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_task_wdt_init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Watchdog was initialized successfully");
    watchdog_enabled = true;
    return ESP_OK;

}

esp_err_t watchdog_deinit() {

    if (!watchdog_enabled) {
        return ESP_OK;
    }
    ret = esp_task_wdt_deinit();                      // Полностью отключаем
    ESP_LOGI(TAG, "Watchdog was disabled");
    watchdog_enabled = false;
    return ret;

}