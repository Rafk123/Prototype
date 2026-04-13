#include "led_manager.h"

static const char *TAG = "LED";

esp_err_t led_init(void) {
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << LED_RED_PIN) | (1ULL << LED_GREEN_PIN),
        .intr_type = GPIO_INTR_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LED pins");
        return ret;
    }
    
    // Выключаем оба LED
    gpio_set_level(LED_RED_PIN, LED_OFF);
    gpio_set_level(LED_GREEN_PIN, LED_OFF);
    
    ESP_LOGI(TAG, "LED initialized");
    return ESP_OK;
}

void led_red_on(void) {
    gpio_set_level(LED_RED_PIN, LED_ON);
}

void led_red_off(void) {
    gpio_set_level(LED_RED_PIN, LED_OFF);
}

void led_green_on(void) {
    gpio_set_level(LED_GREEN_PIN, LED_ON);
}

void led_green_off(void) {
    gpio_set_level(LED_GREEN_PIN, LED_OFF);
}