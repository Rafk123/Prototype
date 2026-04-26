#include "drone_detector.h"

static const char *TAG = "CNN";

//  Параметры
static const int SR = 16000;
static const int N_FFT = 512;
static const int HOP_LENGTH = 256;
static const int N_MELS = 32;
static const int N_FRAMES = 63;
static const int PAD = N_FFT / 2;
static const int PADDED_LEN = SR + 2 * PAD;

//  Для INT8 модели 80-100 КБ обычно хватает за глаза, но оставим 160 для запаса
static const int ARENA_SIZE = 100 * 1024; 

//  Глобальные объекты (УБРАЛИ static для корректной линковки)
static tflite::MicroMutableOpResolver <15> resolver;
static tflite::MicroInterpreter *interpreter = nullptr;
static uint8_t* tensor_arena_buf = nullptr;
static const tflite::Model* model = nullptr;

// Буферы для run_model
static float* padded_audio;
static float* fft_input;
static float* power_spec;
static float* spectrogram_ptr;
static float* window_hann;

esp_err_t setup_system() {
    {
        // 0. Прямая проверка байтов внутри массива модели
        ESP_LOGI(TAG, "[SETUP][DEBUG] Размер drone_model = %u", drone_model_len);
        ESP_LOGI(TAG, "[SETUP][DEBUG] Адрес drone_model: %p", drone_model);
        ESP_LOGI(TAG, "[SETUP][DEBUG] Первые 32 байта: ");
        for(int i = 0; i < 32 && i < drone_model_len; i++) {
            printf("%02x ", drone_model[i]);
        }
        printf("\n");
        const char* magic = (const char*)drone_model;
        if (drone_model_len > 8) {
            ESP_LOGI(TAG, "[SETUP][DEBUG] Магические байты (4-7): %c%c%c%c", magic[4], magic[5], magic[6], magic[7]);
        }
        else {
            ESP_LOGW(TAG, "[SETUP][WARNING] Размер drone_model равно 0!");
        }
        // ПРЯМАЯ ПРОВЕРКА: Если это INT8 модель с Scale 1/255, 
        // мы можем найти эти байты в заголовке или метаданных.
        // Но самый простой способ — проверить смещение к версии.
        uint32_t* header = (uint32_t*)drone_model;
        ESP_LOGI(TAG, "[SETUP][DEBUG] Header[0] (Offset to Root Table): %u", (unsigned int)header[0]);
    }

    {
        // 1. Выделяем буферы: FFT в IRAM, остальное в PSRAM
        // IRAM
        fft_input = (float*)heap_caps_malloc(N_FFT * 2 * sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        window_hann = (float*)heap_caps_malloc(N_FFT * sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        // PSRAM
        padded_audio = (float*)heap_caps_malloc(PADDED_LEN * sizeof(float), MALLOC_CAP_SPIRAM);
        spectrogram_ptr = (float*)heap_caps_malloc(N_MELS * N_FRAMES * sizeof(float), MALLOC_CAP_SPIRAM);
        power_spec = (float*)heap_caps_malloc((N_FFT / 2 + 1) * sizeof(float), MALLOC_CAP_SPIRAM);
        ESP_LOGI(TAG, "[SETUP][LOG] Буферы для run_model выделены");
    }

    {
        // 2. Инициализация таблиц для FFT
        esp_err_t err = dsps_fft2r_init_fc32(NULL, 512); 
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "[SETUP][ERR] Ошибка инициализации FFT таблиц!");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "[SETUP][LOG] ESP-DSP инициализирован");
    }

    {
        // 3. Окно Ханна (ручная инициализация)
        if (window_hann) {
            for (int i = 0; i < N_FFT; i++) {
                window_hann[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (N_FFT - 1)));
            }
        }
        ESP_LOGI(TAG, "[SETUP][LOG] Окно Ханна инициализирована");
    }

    {
        // 4. Выделяем Арену. Для float модели она точно влезет в IRAM, но выделим в PSRAM.
        tensor_arena_buf = (uint8_t*)heap_caps_aligned_alloc(64, ARENA_SIZE, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        if (tensor_arena_buf == nullptr) {
            ESP_LOGE(TAG, "[SETUP][ERR] Не удалось выделить память в IRAM для арены");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "[SETUP][LOG] Для арены память выделена в IRAM (Aligned 64)");
    }

    {
        // 5. Инициализация TFLM
        model = tflite::GetModel(drone_model);
        if (model->version() != TFLITE_SCHEMA_VERSION) {
            ESP_LOGE(TAG, "[SETUP][ERR] Версия битая! Модель: %ld", model->version()); 
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "[SETUP][LOG] Заголовок модели прочтен"); 
    }

    {
        // Тип входного тензора обычно лежит по определенному смещению
        auto subgraphs = model->subgraphs();
        auto inputs = (*subgraphs)[0]->inputs();
        auto tensor = (*(*subgraphs)[0]->tensors())[(*inputs)[0]];
        ESP_LOGI(TAG, "[SETUP][DEBUG] Проверка из массива после GetModel: Тип тензора №0 = %d", (int)tensor->type());
        ESP_LOGI(TAG, "[SETUP][DEBUG] Указатель model: %p", (void*)model);
        ESP_LOGI(TAG, "[SETUP][DEBUG] Schema версия: %ld", model->version());
    }

    {
        // Операторы
        resolver.AddConv2D();
        resolver.AddMaxPool2D();
        resolver.AddFullyConnected();
        resolver.AddRelu();
        resolver.AddLogistic();
        resolver.AddReshape();
        resolver.AddPack();
        resolver.AddUnpack();
        resolver.AddShape();
        resolver.AddMul();
        resolver.AddSub();
        resolver.AddStridedSlice();
        resolver.AddMean();
        resolver.AddAdd();
        resolver.AddPad();
    }

    {
        // 7. Интерпретатор
        static tflite::MicroInterpreter static_interpreter(model, resolver, tensor_arena_buf, ARENA_SIZE);
        interpreter = &static_interpreter;
        // Это поможет понять, не нулевые ли они
        auto* subgraph = model->subgraphs()->Get(0);
        auto* operators = subgraph->operators();
        ESP_LOGI(TAG, "[SETUP][DEBUG] Кол-во операторов в модели: %d", operators->size());
        ESP_LOGI(TAG, "[SETUP][DEBUG] AllocateTensors статус: %d", (int)interpreter->AllocateTensors()); 
        if (interpreter->AllocateTensors() != kTfLiteOk) {
            ESP_LOGE(TAG, "[SETUP][ERR] Ошибка создания интерпретатора");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "[SETUP][LOG] Интерпретатор создан");
    }

    {
        // Проверка на float модели и вывод адресов
        TfLiteTensor* input_tensor = interpreter->input_tensor(0);
        ESP_LOGI(TAG, "[SETUP][DEBUG] Входной тензор: %p", input_tensor->data.f);
        if (input_tensor->data.f == nullptr) {
            ESP_LOGE(TAG, "[SETUP][ERR] Тензоры не получили адреса!");
            return ESP_FAIL;
        } 
        ESP_LOGI(TAG, "[SETUP][LOG] Адрес входного тензора float: %p", input_tensor->data.f);
        ESP_LOGI(TAG, "[SETUP][DEBUG] Арена заняла байты: %d", interpreter->arena_used_bytes());
        ESP_LOGI(TAG, "[SETUP][DEBUG] Тип в интерпретаторе: %d, Scale: %f", (int)input_tensor->type, input_tensor->params.scale);
        ESP_LOGI(TAG, "[SETUP][DEBUG] Адрес созданного интерпретатора: %p", (void*)interpreter);
    }

    return ESP_OK;
}

float run_drone_detection(const float *audio_1s)
{
    ESP_LOGI(TAG, "[RUN][DEBUG] Указатель model: %p", (void*)model);
    // --- ПРОВЕРКА №1: СРАЗУ ПРИ ВХОДЕ ---
    TfLiteTensor* t_dbg = interpreter->input_tensor(0);
    ESP_LOGI(TAG, "[RUN][DEBUG] Тип: %d, Scale: %f", (int)t_dbg->type, t_dbg->params.scale);
    ESP_LOGI(TAG, "[RUN[DEBUG] Указатель interpreter: %p", (void*)interpreter);
    ESP_LOGI(TAG, "[RUN][DEBUG] Входной тензор: %p", t_dbg->data.f);
    float* input_data = interpreter->typed_input_tensor<float>(0);
    if (input_data != nullptr) {
        ESP_LOGI(TAG, "[RUN][DEBUG] Адрес данных получен: %p", input_data);
        // Теперь сюда можно копировать данные
    } else {
        ESP_LOGE(TAG, "[RUN][ERR] Даже typed_input_tensor вернул NULL");
    }

    if (interpreter == nullptr) {
        ESP_LOGE(TAG, "[RUN][ERR] Интерпретатор NULL");
        return -1.0f;
    }

    // 1. Очистка и копирование аудио
    memset(padded_audio, 0, PADDED_LEN * sizeof(float));
    memcpy(padded_audio + PAD, audio_1s, SR * sizeof(float));
    ESP_LOGI(TAG, "[RUN][LOG] Аудио загружено. PAD=%d, SR=%d", PAD, SR);

    // 2. Расчет спектрограммы
    for (int f = 0; f < N_FRAMES; f++) {
        const float *frame = padded_audio + f * HOP_LENGTH;
        for (int i = 0; i < N_FFT; i++) {
            fft_input[i * 2] = frame[i] * window_hann[i];
            fft_input[i * 2 + 1] = 0.0f;
        }

        dsps_fft2r_fc32(fft_input, N_FFT);
        dsps_bit_rev_fc32(fft_input, N_FFT);

        for (int m = 0; m < N_MELS; m++) {
            float mel_val = 0.0f;
            for (int j = 0; j <= N_FFT / 2; j++) {
                float re = fft_input[j * 2];
                float im = fft_input[j * 2 + 1];
                mel_val += (re * re + im * im) * mel_basis[m][j];
            }
            spectrogram_ptr[m * N_FRAMES + f] = mel_val;
        }
    }
    ESP_LOGI(TAG, "[RUN][LOG] Спектрограмма рассчитана (32x63)");

    // 3. Нормализация с логами уровней
    float global_max = 1e-10f;
    for (int i = 0; i < N_MELS * N_FRAMES; i++) {
        if (spectrogram_ptr[i] > global_max) global_max = spectrogram_ptr[i];
    }
    
    float ref_db = 10.0f * log10f(global_max);
    ESP_LOGI(TAG, "[RUN][LOG] Max Energy: %.6f, Ref dB: %.2f", global_max, ref_db);

    for (int i = 0; i < N_MELS * N_FRAMES; i++) {
        float db = 10.0f * log10f(fmaxf(spectrogram_ptr[i], 1e-10f)) - ref_db;
        float norm = (db + 80.0f) / 80.0f;
        spectrogram_ptr[i] = fmaxf(0.0f, fminf(1.0f, norm));
    }

    // 4. Подготовка входного тензора
    // --- ПРОВЕРКА ПЕРЕД ОШИБКОЙ (V3) ---
    TfLiteTensor* input_tensor = interpreter->input_tensor(0);
    ESP_LOGI(TAG, "[RUN][DEBUG] Тензор: тип=%d, scale=%.6f, zp=%d", 
            input_tensor->type, input_tensor->params.scale, input_tensor->params.zero_point);

    if (input_tensor->type != kTfLiteFloat32) {
        ESP_LOGE(TAG, "[RUN][ERR] Ожидался тип 1 (float), в модели тип %d", input_tensor->type);
        return -1.0f;
    }

    float* dst = input_tensor->data.f;

    // Статистика для контроля качества квантования
    float sum_q = 0;
    for (int i = 0; i < N_MELS * N_FRAMES; i++) {
        dst[i] = spectrogram_ptr[i];
        sum_q += dst[i];
    }
    ESP_LOGI(TAG, "[RUN][LOG] Входной тензор заполнен. Ср.знач. тензора: %.2f", sum_q / (N_MELS * N_FRAMES));

    // 5. Инференс
    ESP_LOGI(TAG, "[RUN][LOG] Запуск Invoke...");
    int64_t start_time = esp_timer_get_time();
    TfLiteStatus invoke_status = interpreter->Invoke();
    int64_t end_time = esp_timer_get_time();

    if (invoke_status != kTfLiteOk) {
        ESP_LOGE(TAG, "[RUN][ERR] Invoke не удался!");
        return -1.0f;
    }
    ESP_LOGI(TAG, "[RUN][LOG] Invoke завершен за %lld мкс", end_time - start_time);

    // 6. Деквантование выхода
    TfLiteTensor* output_tensor = interpreter->output(0);
    float result = output_tensor->data.f[0];

    ESP_LOGI(TAG, "[RUN][RESULT] Prob: %.4f -> %s", 
           result, result >= 0.5f ? "!!! DRONE !!!" : "no");

    return result;
}