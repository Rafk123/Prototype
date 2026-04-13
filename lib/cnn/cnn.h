#pragma once

#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
#include <algorithm>
#include <cstring>
#include <cassert>
#include <numeric>
#include <limits>
#include <cstdint>
#include <chrono>

// ============================================================================
// ======================== КОНФИГУРАЦИЯ И HYPER-ПАРАМЕТРЫ =================
// ============================================================================

// ===== MEL SPECTROGRAM HYPER-ПАРАМЕТРЫ =====
const int SAMPLING_RATE = 16000;          // Частота дискретизации (Hz)
const int N_MELS = 64;                    // Количество MEL фильтров
const int N_FFT = 1024;                   // Размер FFT окна
const int HOP_LENGTH = N_FFT / 2;         // Сдвиг окна (512)
const float TRIM_DURATION = 5.0f;         // Длительность аудио (сек)
const float LOG_BASE = 10.0f;             // Основание логарифма
// ==========================================

// ===== АРХИТЕКТУРА НЕЙРОСЕТИ =====
const int NUM_CONV_LAYERS = 3;            // Количество сверточных слоев
const int NUM_FEATURES = 32;              // Базовое количество фильтров
const int MEL_SHAPE_FREQ = 64;            // Частотное измерение MEL спектраграммы
const int MEL_SHAPE_TIME = (int)(TRIM_DURATION * SAMPLING_RATE / HOP_LENGTH) + 1; // = 157
// ==================================

// ============================================================================
// ======================== ТИПЫ ДАННЫХ И СТРУКТУРЫ ==========================
// ============================================================================

// Простая 4D тензор для (batch, channels, height, width)
class Tensor4D {
public:
    std::vector<float> data;
    int batch, channels, height, width;
    
    Tensor4D() : batch(0), channels(0), height(0), width(0) {}
    
    Tensor4D(int b, int c, int h, int w) 
        : batch(b), channels(c), height(h), width(w) {
        data.resize(b * c * h * w, 0.0f);
    }
    
    float& at(int b, int c, int h, int w) {
        return data[((b * channels + c) * height + h) * width + w];
    }
    
    float at(int b, int c, int h, int w) const {
        return data[((b * channels + c) * height + h) * width + w];
    }
    
    int size() const { return data.size(); }
};

// Простая 2D тензор для (height, width)
class Tensor2D {
public:
    std::vector<float> data;
    int height, width;
    
    Tensor2D() : height(0), width(0) {}
    
    Tensor2D(int h, int w) : height(h), width(w) {
        data.resize(h * w, 0.0f);
    }
    
    float& at(int h, int w) {
        return data[h * width + w];
    }
    
    float at(int h, int w) const {
        return data[h * width + w];
    }
};

// Простая 1D тензор для вектора
class Tensor1D {
public:
    std::vector<float> data;
    
    Tensor1D() {}
    Tensor1D(int size) : data(size, 0.0f) {}
    
    float& operator[](int i) { return data[i]; }
    float operator[](int i) const { return data[i]; }
    int size() const { return data.size(); }
};

// ============================================================================
// ========== WAV ФАЙЛ - ЧТЕНИЕ И ПРЕДОБРАБОТКА (WAV PREPROCESSING) ==========
// ============================================================================

// Простая структура для WAV заголовка
struct WavHeader {
    char riff[4];           // "RIFF"
    uint32_t file_size;     // Размер файла - 8
    char wave[4];           // "WAVE"
    char fmt[4];            // "fmt "
    uint32_t fmt_size;      // Размер формата (16 для PCM)
    uint16_t audio_format;  // 1 = PCM
    uint16_t num_channels;  // Количество каналов
    uint32_t sample_rate;   // Частота дискретизации
    uint32_t byte_rate;     // Байт/сек
    uint16_t block_align;   // Блок align
    uint16_t bits_per_sample; // Бит на сэмпл
    char data[4];           // "data"
    uint32_t data_size;     // Размер данных
};

/**
 * Читает WAV файл и возвращает аудио данные
 * Поддерживает 16-bit PCM монофонический и стереоаудио
 * 
 * @param filename Путь к WAV файлу
 * @param audio Выходной вектор с аудио сэмплами (нормализованные [-1, 1])
 * @param sample_rate Выходная частота дискретизации
 * @return true если успешно, false если ошибка
 */
bool read_wav_file(const std::string& filename, std::vector<float>& audio, int& sample_rate) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "ERROR: Не могу открыть файл: " << filename << std::endl;
        return false;
    }
    
    WavHeader header;
    file.read((char*)&header, sizeof(WavHeader));
    
    // Проверяем WAV формат
    if (std::strncmp(header.riff, "RIFF", 4) != 0 || 
        std::strncmp(header.wave, "WAVE", 4) != 0) {
        std::cerr << "ERROR: Неверный WAV формат" << std::endl;
        return false;
    }
    
    sample_rate = header.sample_rate;
    int num_samples = header.data_size / (header.bits_per_sample / 8) / header.num_channels;
    
    audio.resize(num_samples);
    
    // Читаем аудио данные
    if (header.bits_per_sample == 16) {
        std::vector<int16_t> pcm_data(num_samples * header.num_channels);
        file.read((char*)pcm_data.data(), header.data_size);
        
        // Конвертируем в float [-1, 1] и усредняем если стерео
        const float scale = 1.0f / 32768.0f;
        for (int i = 0; i < num_samples; i++) {
            float sum = 0.0f;
            for (int ch = 0; ch < header.num_channels; ch++) {
                sum += pcm_data[i * header.num_channels + ch] * scale;
            }
            audio[i] = sum / header.num_channels;
        }
    } else {
        std::cerr << "ERROR: Поддерживается только 16-bit PCM" << std::endl;
        return false;
    }
    
    file.close();
    return true;
}

/**
 * Обрезает или дублирует аудио до нужной длины
 * Если длина < target_duration, дублирует звук
 * Затем обрезает до точно нужной длины
 * 
 * @param audio Входной аудио вектор
 * @param sr Частота дискретизации
 * @param target_duration Желаемая длительность (сек)
 * @return Обработанный аудио вектор
 */
std::vector<float> pad_or_trim_audio(const std::vector<float>& audio, int sr, float target_duration) {
    int target_samples = (int)(sr * target_duration);
    int current_samples = (int)audio.size();
    
    std::vector<float> result;
    
    if (current_samples >= target_samples) {
        // Просто обрезаем
        result.assign(audio.begin(), audio.begin() + target_samples);
    } else {
        // Дублируем, пока не превысим нужную длину
        while ((int)result.size() < target_samples) {
            result.insert(result.end(), audio.begin(), audio.end());
        }
        result.resize(target_samples);
    }
    
    return result;
}

/**
 * Предобрабатывает WAV файл:
 * 1. Читает WAV файл
 * 2. Ресэмплирует (если нужно) на SAMPLING_RATE
 * 3. Обрезает/дублирует до 5 секунд
 * 
 * @param wav_path Путь к WAV файлу
 * @param output_audio Выходной аудио вектор
 * @return true если успешно
 */
bool preprocess_wav(const std::string& wav_path, std::vector<float>& output_audio) {
    std::vector<float> audio;
    int sample_rate;
    
    if (!read_wav_file(wav_path, audio, sample_rate)) {
        return false;
    }
    
    // Если нужна ресэмплирование, делаем простое интерполирование
    if (sample_rate != SAMPLING_RATE) {
        std::vector<float> resampled;
        float ratio = (float)sample_rate / SAMPLING_RATE;
        int target_size = (int)(audio.size() / ratio);
        resampled.resize(target_size);
        
        for (int i = 0; i < target_size; i++) {
            float src_idx = i * ratio;
            int idx = (int)src_idx;
            float frac = src_idx - idx;
            
            if (idx + 1 < (int)audio.size()) {
                resampled[i] = audio[idx] * (1.0f - frac) + audio[idx + 1] * frac;
            } else {
                resampled[i] = audio[idx];
            }
        }
        
        audio = resampled;
    }
    
    // Обрезаем/дублируем до 5 секунд
    output_audio = pad_or_trim_audio(audio, SAMPLING_RATE, TRIM_DURATION);
    
    return true;
}

// ============================================================================
// ============= FFT И MEL SPECTROGRAM (СПЕКТРОГРАММА) =======================
// ============================================================================

// Простое FFT использование std::complex
void fft_recursive(std::vector<std::complex<float>>& x) {
    int N = x.size();
    if (N <= 1) return;
    
    // Разделение на четные и нечетные
    std::vector<std::complex<float>> even, odd;
    for (int i = 0; i < N; i += 2) {
        even.push_back(x[i]);
    }
    for (int i = 1; i < N; i += 2) {
        odd.push_back(x[i]);
    }
    
    // Рекурсивное FFT
    fft_recursive(even);
    fft_recursive(odd);
    
    // Комбинирование результатов
    for (int k = 0; k < N / 2; k++) {
        std::complex<float> t = std::polar(1.0f, -2.0f * (float)M_PI * k / N) * odd[k];
        x[k] = even[k] + t;
        x[k + N / 2] = even[k] - t;
    }
}

/**
 * Вычисляет Short-Time Fourier Transform (STFT) для аудиосигнала
 * 
 * @param audio Входной аудио вектор
 * @param n_fft Размер FFT окна
 * @param hop_length Сдвиг окна
 * @return 2D матрица спектра (частотные бины, временные фреймы)
 */
Tensor2D compute_stft(const std::vector<float>& audio, int n_fft, int hop_length) {
    int num_frames = ((int)audio.size() - n_fft) / hop_length + 1;
    int num_freqs = n_fft / 2 + 1;
    
    Tensor2D stft(num_freqs, num_frames);
    
    // Hann окно
    std::vector<float> window(n_fft);
    for (int i = 0; i < n_fft; i++) {
        window[i] = 0.5f * (1.0f - std::cos(2.0f * (float)M_PI * i / (n_fft - 1)));
    }
    
    // Вычисляем STFT для каждого фрейма
    for (int frame = 0; frame < num_frames; frame++) {
        std::vector<std::complex<float>> frame_data(n_fft, 0.0f);
        
        // Применяем окно
        for (int i = 0; i < n_fft; i++) {
            int audio_idx = frame * hop_length + i;
            if (audio_idx < (int)audio.size()) {
                frame_data[i] = audio[audio_idx] * window[i];
            }
        }
        
        // FFT
        fft_recursive(frame_data);
        
        // Вычисляем мощность спектра (берем только первую половину)
        for (int freq = 0; freq < num_freqs; freq++) {
            float mag = std::abs(frame_data[freq]);
            stft.at(freq, frame) = mag * mag;  // Мощность
        }
    }
    
    return stft;
}

/**
 * Создает MEL фильтербанк
 * Преобразует линейную шкалу частот в мелологарифмическую шкалу
 * 
 * @param sr Частота дискретизации
 * @param n_fft Размер FFT
 * @param n_mels Количество MEL фильтров
 * @return Матрица весов фильтербанка (n_mels, n_fft/2 + 1)
 */
Tensor2D create_mel_filterbank(int sr, int n_fft, int n_mels) {
    int num_freqs = n_fft / 2 + 1;
    Tensor2D mel_filter(n_mels, num_freqs);
    
    // Граничные частоты в MEL
    auto hz_to_mel = [](float hz) {
        return 2595.0f * std::log10(1.0f + hz / 700.0f);
    };
    
    auto mel_to_hz = [](float mel) {
        return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f);
    };
    
    float mel_min = hz_to_mel(0.0f);
    float mel_max = hz_to_mel((float)sr / 2.0f);
    
    // Граничные частоты фильтров в MEL
    std::vector<float> mel_points(n_mels + 2);
    for (int i = 0; i < n_mels + 2; i++) {
        mel_points[i] = mel_min + (mel_max - mel_min) * i / (n_mels + 1);
    }
    
    // Преобразуем обратно в Hz
    std::vector<float> hz_points(n_mels + 2);
    for (int i = 0; i < n_mels + 2; i++) {
        hz_points[i] = mel_to_hz(mel_points[i]);
    }
    
    // Создаем фильтры
    for (int m = 0; m < n_mels; m++) {
        float left_hz = hz_points[m];
        float center_hz = hz_points[m + 1];
        float right_hz = hz_points[m + 2];
        
        for (int k = 0; k < num_freqs; k++) {
            float freq = (float)k * sr / n_fft;
            
            if (freq >= left_hz && freq <= right_hz) {
                if (freq < center_hz) {
                    mel_filter.at(m, k) = (freq - left_hz) / (center_hz - left_hz);
                } else {
                    mel_filter.at(m, k) = (right_hz - freq) / (right_hz - center_hz);
                }
            }
        }
    }
    
    // Нормализуем фильтры (как librosa с norm='slaney')
    for (int m = 0; m < n_mels; m++) {
        float enorm = 2.0f / (hz_points[m + 2] - hz_points[m]);
        for (int k = 0; k < num_freqs; k++) {
            mel_filter.at(m, k) *= enorm;
        }
    }
    
    return mel_filter;
}

/**
 * Вычисляет MEL спектрограмму из аудио
 * Процесс: Audio -> STFT -> Power -> MEL Filterbank -> Log (base 10)
 * 
 * @param audio Входной аудио вектор
 * @param sr Частота дискретизации
 * @param n_mels Количество MEL фильтров
 * @param n_fft Размер FFT
 * @param hop_length Сдвиг окна
 * @return 2D матрица MEL спектрограммы (n_mels, num_frames)
 */
Tensor2D compute_mel_spectrogram(const std::vector<float>& audio, int sr, int n_mels, int n_fft, int hop_length) {
    // Шаг 1: Вычисляем STFT
    Tensor2D stft = compute_stft(audio, n_fft, hop_length);
    
    // Шаг 2: Создаем MEL фильтербанк
    Tensor2D mel_filter = create_mel_filterbank(sr, n_fft, n_mels);
    
    // Шаг 3: Применяем фильтербанк
    Tensor2D mel_spec(n_mels, stft.width);
    
    for (int m = 0; m < n_mels; m++) {
        for (int t = 0; t < stft.width; t++) {
            float sum = 0.0f;
            for (int k = 0; k < stft.height; k++) {
                sum += stft.at(k, t) * mel_filter.at(m, k);
            }
            mel_spec.at(m, t) = sum;
        }
    }
    
    // Шаг 4: Логарифмируем (база 10)
    for (int m = 0; m < n_mels; m++) {
        for (int t = 0; t < stft.width; t++) {
            float val = mel_spec.at(m, t);
            mel_spec.at(m, t) = std::log10(val + 1e-9f);
        }
    }
    
    return mel_spec;
}

// ============================================================================
// ============== НЕЙРОСЕТЕВЫЕ СЛОИ (NEURAL NETWORK LAYERS) ===================
// ============================================================================

/**
 * ReLU активационная функция
 * f(x) = max(0, x)
 */
float relu(float x) {
    return x > 0.0f ? x : 0.0f;
}

/**
 * Sigmoid активационная функция
 * f(x) = 1 / (1 + exp(-x))
 */
float sigmoid(float x) {
    if (x >= 0.0f) {
        float z = std::exp(-x);
        return 1.0f / (1.0f + z);
    } else {
        float z = std::exp(x);
        return z / (1.0f + z);
    }
}

/**
 * Conv2d слой (упрощенная версия)
 * Применяет 3x3 сверточный фильтр с padding=1
 */
class Conv2d {
public:
    std::vector<Tensor4D> weights;  // (out_channels, in_channels, 3, 3)
    std::vector<float> bias;         // (out_channels,)
    int in_channels, out_channels;
    
    Conv2d(int in_ch, int out_ch) : in_channels(in_ch), out_channels(out_ch) {
        weights.resize(out_ch);
        for (int o = 0; o < out_ch; o++) {
            weights[o] = Tensor4D(1, in_ch, 3, 3);
        }
        bias.resize(out_ch, 0.0f);
    }
    
    Tensor4D forward(const Tensor4D& input) {
        Tensor4D output(input.batch, out_channels, input.height, input.width);
        
        for (int b = 0; b < input.batch; b++) {
            for (int o = 0; o < out_channels; o++) {
                for (int h = 0; h < input.height; h++) {
                    for (int w = 0; w < input.width; w++) {
                        float sum = bias[o];
                        
                        // 3x3 конволюция с padding
                        for (int kh = 0; kh < 3; kh++) {
                            for (int kw = 0; kw < 3; kw++) {
                                int ih = h + kh - 1;
                                int iw = w + kw - 1;
                                
                                if (ih >= 0 && ih < input.height && iw >= 0 && iw < input.width) {
                                    for (int i = 0; i < in_channels; i++) {
                                        sum += input.at(b, i, ih, iw) * weights[o].at(0, i, kh, kw);
                                    }
                                }
                            }
                        }
                        
                        output.at(b, o, h, w) = sum;
                    }
                }
            }
        }
        
        return output;
    }
};

/**
 * BatchNorm2d слой (упрощенная версия - только инференс)
 */
class BatchNorm2d {
public:
    std::vector<float> weight;        // gamma (channels,)
    std::vector<float> bias;          // beta (channels,)
    std::vector<float> running_mean;  // running_mean (channels,)
    std::vector<float> running_var;   // running_variance (channels,)
    int channels;
    float eps;
    
    BatchNorm2d(int ch, float epsilon = 1e-5f) : channels(ch), eps(epsilon) {
        weight.resize(ch, 1.0f);
        bias.resize(ch, 0.0f);
        running_mean.resize(ch, 0.0f);
        running_var.resize(ch, 1.0f);
    }
    
    Tensor4D forward(const Tensor4D& input) {
        Tensor4D output(input.batch, channels, input.height, input.width);
        
        for (int b = 0; b < input.batch; b++) {
            for (int c = 0; c < channels; c++) {
                for (int h = 0; h < input.height; h++) {
                    for (int w = 0; w < input.width; w++) {
                        // Нормализуем как: (x - mean) / sqrt(var + eps)
                        float normalized = (input.at(b, c, h, w) - running_mean[c]) / 
                                         std::sqrt(running_var[c] + eps);
                        // Применяем gamma и beta: y = gamma * normalized + beta
                        output.at(b, c, h, w) = normalized * weight[c] + bias[c];
                    }
                }
            }
        }
        
        return output;
    }
};

/**
 * ReLU слой
 */
Tensor4D apply_relu(const Tensor4D& input) {
    Tensor4D output = input;
    for (int i = 0; i < output.size(); i++) {
        output.data[i] = relu(output.data[i]);
    }
    return output;
}

/**
 * MaxPool2d слой (2x2 pooling)
 */
Tensor4D apply_max_pool2d(const Tensor4D& input, int kernel_size = 2) {
    int new_height = input.height / kernel_size;
    int new_width = input.width / kernel_size;
    Tensor4D output(input.batch, input.channels, new_height, new_width);
    
    for (int b = 0; b < input.batch; b++) {
        for (int c = 0; c < input.channels; c++) {
            for (int h = 0; h < new_height; h++) {
                for (int w = 0; w < new_width; w++) {
                    float max_val = -std::numeric_limits<float>::infinity();
                    
                    for (int kh = 0; kh < kernel_size; kh++) {
                        for (int kw = 0; kw < kernel_size; kw++) {
                            int ih = h * kernel_size + kh;
                            int iw = w * kernel_size + kw;
                            max_val = std::max(max_val, input.at(b, c, ih, iw));
                        }
                    }
                    
                    output.at(b, c, h, w) = max_val;
                }
            }
        }
    }
    
    return output;
}

/**
 * Global Average Pooling
 */
Tensor2D apply_global_avg_pool(const Tensor4D& input) {
    Tensor2D output(input.batch, input.channels);
    
    for (int b = 0; b < input.batch; b++) {
        for (int c = 0; c < input.channels; c++) {
            float sum = 0.0f;
            int count = input.height * input.width;
            
            for (int h = 0; h < input.height; h++) {
                for (int w = 0; w < input.width; w++) {
                    sum += input.at(b, c, h, w);
                }
            }
            
            output.at(b, c) = sum / count;
        }
    }
    
    return output;
}

// ============================================================================
// =============== ПОЛНАЯ НЕЙРОСЕТЕВАЯ АРХИТЕКТУРА (CNN MODEL) ================
// ============================================================================

class FanClassifierCNN {
public:
    // Сверточные блоки
    std::vector<Conv2d> conv_layers;
    std::vector<BatchNorm2d> bn_layers;
    
    // Полносвязный слой
    Tensor1D fc_weight;  // (1, feature_size)
    float fc_bias;
    
    int feature_size;
    
    FanClassifierCNN() : feature_size(128) {  // 128 = 32 * 2^2
        // Инициализируем сверточные слои
        conv_layers.push_back(Conv2d(1, 32));    // Conv0: in=1, out=32
        conv_layers.push_back(Conv2d(32, 64));   // Conv1: in=32, out=64
        conv_layers.push_back(Conv2d(64, 128));  // Conv2: in=64, out=128
        
        // Инициализируем BatchNorm слои
        bn_layers.push_back(BatchNorm2d(32));
        bn_layers.push_back(BatchNorm2d(64));
        bn_layers.push_back(BatchNorm2d(128));
        
        // Инициализируем FC слой
        fc_weight = Tensor1D(feature_size);
        fc_bias = 0.0f;
    }
    
    /**
     * Forward pass через всю сеть
     * Architecture:
     * Input (1, 64, 157)
     *   -> Conv2d + BatchNorm + ReLU + MaxPool  (32 channels)
     *   -> Conv2d + BatchNorm + ReLU + MaxPool  (64 channels)
     *   -> Conv2d + BatchNorm + ReLU + MaxPool  (128 channels)
     *   -> Global Average Pooling  (128,)
     *   -> FC (1,)
     *   -> Sigmoid
     * Output: logit [0, 1]
     */
    float forward(const Tensor2D& mel_spec) {
        // Подготавливаем входной тензор (batch=1, channels=1, height=64, width=157)
        Tensor4D x(1, 1, mel_spec.height, mel_spec.width);
        for (int h = 0; h < mel_spec.height; h++) {
            for (int w = 0; w < mel_spec.width; w++) {
                x.at(0, 0, h, w) = mel_spec.at(h, w);
            }
        }
        
        // Слой 1: Conv2d(1, 32) + BatchNorm + ReLU + MaxPool
        x = conv_layers[0].forward(x);
        x = bn_layers[0].forward(x);
        x = apply_relu(x);
        x = apply_max_pool2d(x);
        
        // Слой 2: Conv2d(32, 64) + BatchNorm + ReLU + MaxPool
        x = conv_layers[1].forward(x);
        x = bn_layers[1].forward(x);
        x = apply_relu(x);
        x = apply_max_pool2d(x);
        
        // Слой 3: Conv2d(64, 128) + BatchNorm + ReLU + MaxPool
        x = conv_layers[2].forward(x);
        x = bn_layers[2].forward(x);
        x = apply_relu(x);
        x = apply_max_pool2d(x);
        
        // Global Average Pooling
        Tensor2D pooled = apply_global_avg_pool(x);
        
        // Fully Connected слой
        float logit = fc_bias;
        for (int i = 0; i < feature_size; i++) {
            logit += pooled.at(0, i) * fc_weight[i];
        }
        
        float output = sigmoid(logit);
        return output;
    }
};

// ============================================================================
// ====== ЗАГРУЗКА ВЕСОВ (WEIGHT LOADING FROM BINARY FORMAT) =======================
// ============================================================================

/**
 * Читает бинарный файл с тензором (создан в Python)
 * Формат: [shape_dims] [shape_values...] [data_values...]
 */
std::vector<float> load_tensor_from_binary(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "WARNING: Не могу открыть файл: " << filepath << std::endl;
        return std::vector<float>();
    }
    
    uint32_t shape_dims;
    file.read((char*)&shape_dims, sizeof(uint32_t));
    
    std::vector<uint32_t> shape(shape_dims);
    for (uint32_t i = 0; i < shape_dims; i++) {
        file.read((char*)&shape[i], sizeof(uint32_t));
    }
    
    // Вычисляем количество элементов
    uint32_t num_elements = 1;
    for (uint32_t dim : shape) {
        num_elements *= dim;
    }
    
    // Читаем данные
    std::vector<float> data(num_elements);
    for (uint32_t i = 0; i < num_elements; i++) {
        file.read((char*)&data[i], sizeof(float));
    }
    
    file.close();
    return data;
}

void load_model_weights(FanClassifierCNN& model, const std::string& weights_dir) {
    // Загружаем веса для каждого Conv слоя
    for (int layer = 0; layer < NUM_CONV_LAYERS; layer++) {
        int in_ch = (layer == 0) ? 1 : (32 * (1 << (layer - 1)));
        int out_ch = 32 * (1 << layer);
        
        // Загружаем веса Conv слоя
        std::string weight_file = weights_dir + "/" + std::to_string(layer) + "0w.bin";
        auto weights = load_tensor_from_binary(weight_file);
        
        if (!weights.empty()) {
            int idx = 0;
            for (int o = 0; o < out_ch && idx < (int)weights.size(); o++) {
                for (int i = 0; i < in_ch && idx < (int)weights.size(); i++) {
                    for (int kh = 0; kh < 3 && idx < (int)weights.size(); kh++) {
                        for (int kw = 0; kw < 3 && idx < (int)weights.size(); kw++) {
                            model.conv_layers[layer].weights[o].at(0, i, kh, kw) = weights[idx++];
                        }
                    }
                }
            }
        }
        
        // Загружаем bias Conv слоя
        std::string bias_file = weights_dir + "/" + std::to_string(layer) + "0b.bin";
        auto biases = load_tensor_from_binary(bias_file);
        if (!biases.empty()) {
            for (int o = 0; o < out_ch && o < (int)biases.size(); o++) {
                model.conv_layers[layer].bias[o] = biases[o];
            }
        }
        
        // Загружаем BatchNorm - weight (gamma)
        std::string bn_weight_file = weights_dir + "/" + std::to_string(layer) + "1w.bin";
        auto bn_weights = load_tensor_from_binary(bn_weight_file);
        if (!bn_weights.empty()) {
            for (int c = 0; c < out_ch && c < (int)bn_weights.size(); c++) {
                model.bn_layers[layer].weight[c] = bn_weights[c];
            }
        }
        
        // Загружаем BatchNorm - bias (beta)
        std::string bn_bias_file = weights_dir + "/" + std::to_string(layer) + "1b.bin";
        auto bn_biases = load_tensor_from_binary(bn_bias_file);
        if (!bn_biases.empty()) {
            for (int c = 0; c < out_ch && c < (int)bn_biases.size(); c++) {
                model.bn_layers[layer].bias[c] = bn_biases[c];
            }
        }
        
        // Загружаем BatchNorm - running_mean
        std::string bn_mean_file = weights_dir + "/" + std::to_string(layer) + "1rm.bin";
        auto bn_means = load_tensor_from_binary(bn_mean_file);
        if (!bn_means.empty()) {
            for (int c = 0; c < out_ch && c < (int)bn_means.size(); c++) {
                model.bn_layers[layer].running_mean[c] = bn_means[c];
            }
        }
        
        // Загружаем BatchNorm - running_var
        std::string bn_var_file = weights_dir + "/" + std::to_string(layer) + "1rv.bin";
        auto bn_vars = load_tensor_from_binary(bn_var_file);
        if (!bn_vars.empty()) {
            for (int c = 0; c < out_ch && c < (int)bn_vars.size(); c++) {
                model.bn_layers[layer].running_var[c] = bn_vars[c];
            }
        }
    }  // Конец for (layer)
    
    // Загружаем FC веса
    std::string fc_weight_file = weights_dir + "/fcw.bin";
    auto fc_weights = load_tensor_from_binary(fc_weight_file);
    if (!fc_weights.empty()) {
        for (int i = 0; i < model.feature_size && i < (int)fc_weights.size(); i++) {
            model.fc_weight[i] = fc_weights[i];
        }
    }
    
    // Загружаем FC bias
    std::string fc_bias_file = weights_dir + "/fcb.bin";
    auto fc_biases = load_tensor_from_binary(fc_bias_file);
    if (!fc_biases.empty()) {
        model.fc_bias = fc_biases[0];
    }
}

// ============================================================================
// ================================= MAIN =====================================
// ============================================================================

float cnn_processing(const std::string& wav_file_path, const std::string& model_weights_path) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::vector<float> audio;
    if (!preprocess_wav(wav_file_path, audio)) {
        return -1;
    }
    
    // DEBUG: Выводим информацию об аудио
    float audio_min = audio[0];
    float audio_max = audio[0];
    float audio_sum = 0.0f;
    for (float v : audio) {
        audio_min = std::min(audio_min, v);
        audio_max = std::max(audio_max, v);
        audio_sum += v;
    }
    float audio_mean = audio_sum / audio.size();
    std::cerr << "DEBUG: Audio loaded: size=" << audio.size() << std::endl;
    std::cerr << "DEBUG: Audio min=" << std::fixed << std::setprecision(6) << audio_min
              << ", max=" << audio_max << ", mean=" << audio_mean << std::endl;
    
    Tensor2D mel_spec = compute_mel_spectrogram(audio, SAMPLING_RATE, N_MELS, N_FFT, HOP_LENGTH);
    
    // DEBUG: Выводим информацию о спектре мощности ДО логарифма
    // Нужно пересчитать, без логарифма
    Tensor2D stft_result = compute_stft(audio, N_FFT, HOP_LENGTH);
    float stft_min = stft_result.data[0];
    float stft_max = stft_result.data[0];
    for (float v : stft_result.data) {
        stft_min = std::min(stft_min, v);
        stft_max = std::max(stft_max, v);
    }
    std::cerr << "DEBUG: STFT power min=" << std::fixed << std::setprecision(6) << stft_min
              << ", max=" << stft_max << std::endl;
    std::cerr << "DEBUG: STFT[0,0:10] power: ";
    for (int i = 0; i < std::min(10, stft_result.width); i++) {
        std::cerr << std::fixed << std::setprecision(6) << stft_result.at(0, i) << " ";
    }
    std::cerr << std::endl;
    
    // DEBUG: Выводим информацию о спектре мощности после MEL ДО логарифма
    Tensor2D mel_filter = create_mel_filterbank(SAMPLING_RATE, N_FFT, N_MELS);
    Tensor2D mel_power(N_MELS, stft_result.width);
    for (int m = 0; m < N_MELS; m++) {
        for (int t = 0; t < stft_result.width; t++) {
            float sum = 0.0f;
            for (int k = 0; k < stft_result.height; k++) {
                sum += stft_result.at(k, t) * mel_filter.at(m, k);
            }
            mel_power.at(m, t) = sum;
        }
    }
    float mel_min = mel_power.data[0];
    float mel_max = mel_power.data[0];
    for (float v : mel_power.data) {
        mel_min = std::min(mel_min, v);
        mel_max = std::max(mel_max, v);
    }
    std::cerr << "DEBUG: MEL power (before log) min=" << std::fixed << std::setprecision(6) << mel_min
              << ", max=" << mel_max << std::endl;
    std::cerr << "DEBUG: MEL[0,0:10] power: ";
    for (int i = 0; i < std::min(10, mel_power.width); i++) {
        std::cerr << mel_power.at(0, i) << " ";
    }
    std::cerr << std::endl;
    
    // DEBUG: Выводим информацию о спектре
    float min_val = mel_spec.data[0];
    float max_val = mel_spec.data[0];
    float sum_val = 0.0f;
    for (float v : mel_spec.data) {
        min_val = std::min(min_val, v);
        max_val = std::max(max_val, v);
        sum_val += v;
    }
    float mean_val = sum_val / mel_spec.data.size();
    std::cerr << "DEBUG: MEL спектр shape: (" << mel_spec.height << ", " << mel_spec.width << ")" << std::endl;
    std::cerr << "DEBUG: MEL min: " << std::fixed << std::setprecision(6) << min_val 
              << ", max: " << max_val << ", mean: " << mean_val << std::endl;
    std::cerr << "DEBUG: First row (mel_spec[0,0:10]): ";
    for (int i = 0; i < std::min(10, mel_spec.width); i++) {
        std::cerr << mel_spec.at(0, i) << " ";
    }
    std::cerr << std::endl;
    
    FanClassifierCNN model;
    load_model_weights(model, model_weights_path);
    //"./cpp_model_weights"
    float prediction = model.forward(mel_spec);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    return prediction;
}
