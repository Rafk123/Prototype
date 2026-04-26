// cnn_wrapper.cpp
#include "cnn_wrapper.h"
#include "drone_detector.h"
#include "audio_data.h"

static const char *TAG = "CNN";

float my_wav_data[16000];

void wrapper(void)
{
    ESP_LOGI(TAG, "WRAPPER: START");
    for (int i = 0; i < 16000; i++) {
        my_wav_data[i] = test_audio[i] / 32768.0f;
    }
    ESP_LOGI(TAG, "WRAPPER: audio converted, samples=16000");

    ESP_LOGI(TAG, "WRAPPER: calling setup_system");
    if (setup_system() != ESP_OK) {
        ESP_LOGE(TAG, "WRAPPER: setup is failed!");
        return;
    }
    ESP_LOGI(TAG, "WRAPPER: setup_system returned");

    ESP_LOGI(TAG, "WRAPPER: calling run_drone_detection");
    float result = run_drone_detection(my_wav_data);
    ESP_LOGI(TAG, "WRAPPER: run_drone_detection returned, result=%f", result);

    if (result >= 0)
    {
        ESP_LOGI(TAG, "WRAPPER: Result: %.4f", result);
    }
    else
    {
        ESP_LOGI(TAG, "WRAPPER: The Inference Error!");
    }
    ESP_LOGI(TAG, "WRAPPER: END");
}