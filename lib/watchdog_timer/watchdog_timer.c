#include <watchdog_timer.h>

// Тег для системы логирования
static const char *TAG = "WATCHDOG";
// Общая переменная для хранения кода ошибки
static esp_err_t ret;

esp_err_t watchdog_init() {

    // Если включено, ничего не делаем
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

// Деинициализация сторожевого пса, рекомендуется его выключать
// Сторожевой пес следит за тем, чтобы программа не застряла в цикле без результата
// К примеру, если нейросеть зависнет в вычислении массивов чисел на более, чем минуту, 
// пес гарантированно будет высылать предупреждение и не даст задаче завершиться
esp_err_t watchdog_deinit() {
    // Проверка
    if (!watchdog_enabled) {
        return ESP_OK;
    }
    ret = esp_task_wdt_deinit();                      // Полностью отключаем
    ESP_LOGI(TAG, "Watchdog was disabled");
    watchdog_enabled = false;
    return ret;

}