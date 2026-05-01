#include "localization.h"
#include "esp_log.h"
#include "dsps_fft2r.h"
#include <math.h>

static const char *TAG = "LOCALIZE";

static float *fft_window;
static float *fft_in1, *fft_in2, *fft_in3, *fft_in4;
static float *fft_out1, *fft_out2, *fft_out3, *fft_out4;
static float *gcc_phat_x, *gcc_phat_y;   // для результатов IFFT

esp_err_t localization_init(void) {
    // Выделение памяти в PSRAM (выравнивание под FFT)
    fft_window = heap_caps_malloc(FFT_SIZE * sizeof(float), MALLOC_CAP_SPIRAM);
    fft_in1    = heap_caps_malloc(FFT_SIZE * 2 * sizeof(float), MALLOC_CAP_SPIRAM);
    fft_in2    = heap_caps_malloc(FFT_SIZE * 2 * sizeof(float), MALLOC_CAP_SPIRAM);
    fft_in3    = heap_caps_malloc(FFT_SIZE * 2 * sizeof(float), MALLOC_CAP_SPIRAM);
    fft_in4    = heap_caps_malloc(FFT_SIZE * 2 * sizeof(float), MALLOC_CAP_SPIRAM);
    fft_out1   = heap_caps_malloc(FFT_SIZE * 2 * sizeof(float), MALLOC_CAP_SPIRAM);
    fft_out2   = heap_caps_malloc(FFT_SIZE * 2 * sizeof(float), MALLOC_CAP_SPIRAM);
    fft_out3   = heap_caps_malloc(FFT_SIZE * 2 * sizeof(float), MALLOC_CAP_SPIRAM);
    fft_out4   = heap_caps_malloc(FFT_SIZE * 2 * sizeof(float), MALLOC_CAP_SPIRAM);
    gcc_phat_x = heap_caps_malloc(FFT_SIZE * 2 * sizeof(float), MALLOC_CAP_SPIRAM);
    gcc_phat_y = heap_caps_malloc(FFT_SIZE * 2 * sizeof(float), MALLOC_CAP_SPIRAM);

    if (!fft_window || !fft_in1 || !fft_in2 || !fft_in3 || !fft_in4 ||
        !fft_out1 || !fft_out2 || !fft_out3 || !fft_out4 ||
        !gcc_phat_x || !gcc_phat_y) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        return ESP_FAIL;
    }

    // Окно Ханна
    for (int i = 0; i < FFT_SIZE; i++)
        fft_window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (FFT_SIZE - 1)));

    // Инициализация таблиц FFT (один раз)
    esp_err_t ret = dsps_fft2r_init_fc32(NULL, FFT_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FFT init failed");
        return ret;
    }
    ESP_LOGI(TAG, "Localization engine initialized");
    return ESP_OK;
}

// Внутренняя функция GCC-PHAT между двумя каналами
static int find_delay_samples(const int16_t *a, const int16_t *b,
                              float *in_a, float *in_b,
                              float *out_a, float *out_b,
                              float *gcc) {
    // Заполняем реальные части, применяем окно
    for (int i = 0; i < FFT_SIZE; i++) {
        in_a[i*2]   = (float)a[i] * fft_window[i];
        in_a[i*2+1] = 0.0f;
        in_b[i*2]   = (float)b[i] * fft_window[i];
        in_b[i*2+1] = 0.0f;
    }
    // FFT
    dsps_fft2r_fc32(in_a, FFT_SIZE);
    dsps_bit_rev_fc32(in_a, FFT_SIZE);
    dsps_fft2r_fc32(in_b, FFT_SIZE);
    dsps_bit_rev_fc32(in_b, FFT_SIZE);

    // GCC-PHAT: R = (A * conj(B)) / (|A * conj(B)| + eps)
    const float eps = 1e-6f;
    for (int i = 0; i < FFT_SIZE; i++) {
        float ar = in_a[i*2], ai = in_a[i*2+1];
        float br = in_b[i*2], bi = in_b[i*2+1];
        // conj(B) = br - j*bi
        float cr = ar * br + ai * bi;   // real part of A*conj(B)
        float ci = ai * br - ar * bi;   // imag part
        float mag = sqrtf(cr*cr + ci*ci) + eps;
        gcc[i*2]   = cr / mag;
        gcc[i*2+1] = -ci / mag;         // negate для правильной фазы (опционально)
    }

    // Обратное FFT
    dsps_fft2r_fc32(gcc, FFT_SIZE);
    dsps_bit_rev_fc32(gcc, FFT_SIZE);

    // Поиск максимума (задержка в сэмплах)
    float max_val = 0.0f;
    int max_idx = 0;
    for (int i = 0; i < FFT_SIZE; i++) {
        float val = sqrtf(gcc[i*2]*gcc[i*2] + gcc[i*2+1]*gcc[i*2+1]);
        if (val > max_val) {
            max_val = val;
            max_idx = i;
        }
    }
    // Коррекция для положительного лага
    if (max_idx > FFT_SIZE / 2) max_idx -= FFT_SIZE;
    return max_idx;
}

float localization_calculate_azimuth(const int16_t *ch1, const int16_t *ch2,
                                     const int16_t *ch3, const int16_t *ch4) {
    // Задержка по оси X: mic1 (ch1) и mic3 (ch3)
    int delay_x = find_delay_samples(ch1, ch3, fft_in1, fft_in3,
                                     fft_out1, fft_out3, gcc_phat_x);
    // Задержка по оси Y: mic2 (ch2) и mic4 (ch4)
    int delay_y = find_delay_samples(ch2, ch4, fft_in2, fft_in4,
                                     fft_out2, fft_out4, gcc_phat_y);

    float dx = (float)delay_x * SOUND_SPEED / SAMPLE_RATE;
    float dy = (float)delay_y * SOUND_SPEED / SAMPLE_RATE;

    float sin_x = dx / MIC_DISTANCE;
    float sin_y = dy / MIC_DISTANCE;
    sin_x = fmaxf(-1.0f, fminf(1.0f, sin_x));
    sin_y = fmaxf(-1.0f, fminf(1.0f, sin_y));

    float angle_rad = atan2f(sin_y, sin_x);
    float azimuth = angle_rad * 180.0f / M_PI;
    if (azimuth < 0) azimuth += 360.0f;
    return azimuth;
}
