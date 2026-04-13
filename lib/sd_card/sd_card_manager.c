#include "sd_card_manager.h"

// Тег для системы логирования
static const char *TAG = "SD_CARD";
// Общая переменная для хранения кода ошибки
static esp_err_t ret;

esp_err_t sd_init() {

    // ========== Инициализация SD карты ==========

    // А вдруг смонтировалось?
    if (sd_mounted) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing SD card...");

    // Это конфигурация для SPI интерфейса
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_MOSI,
        .miso_io_num = SD_MISO,
        .sclk_io_num = SD_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 1024,
    };

    // Загружаем конфигурацию
    ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    // В случае, если не инициализировалось
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Конфигурация для самой SD-карты
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = SD_FREQUENCY;

    sdspi_device_config_t slot_config = {
        .gpio_cs = SD_CS,
        .host_id = SPI2_HOST,
        .gpio_cd = SDSPI_SLOT_NO_CD,
        .gpio_wp = SDSPI_SLOT_NO_WP,
        .gpio_int = SDSPI_SLOT_NO_INT,
    };

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 1,
        .allocation_unit_size = 4096,
    };

    // Аналогичный предыдущему процесс
    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(ret));
        spi_bus_free(SPI2_HOST);
        return ret;
    }

    // Флаг, лог и метаданные
    sd_mounted = true;
    ESP_LOGI(TAG, "SD card mounted");
    sdmmc_card_print_info(stdout, card);
    
    // Критически важно (На самом деле может работать без этого): 
    // даем время файловой системе стабилизироваться
    vTaskDelay(pdMS_TO_TICKS(200));

    return ESP_OK;

}

esp_err_t sd_disable() {

    // Если уже выключено, ничего не делаем
    if (!sd_mounted) {
        return ESP_OK;
    }

    // Все освобождаем
    ESP_LOGI(TAG, "SD unmounting...");
    esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
    sdspi_host_deinit();
    spi_bus_free(SPI2_HOST);
    ESP_LOGI(TAG, "SD disabled");

    // Меняем флаг
    sd_mounted = false;
    return ESP_OK;

}