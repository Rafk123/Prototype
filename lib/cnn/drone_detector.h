#ifndef DRONE_DETECTOR_H
#define DRONE_DETECTOR_H

#include "app_config.h" 
#include "esp_memory_utils.h" 
#include "esp_timer.h" // Для esp_timer_get_time()
#include "dsps_fft2r.h"
#include "mel_data.h"
#include "model_data.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/micro/tflite_bridge/micro_error_reporter.h"

extern const unsigned char drone_model_INT8_final[];
extern const unsigned int drone_model_INT8_final_len;

esp_err_t setup_system();
float run_drone_detection(const float*);

#endif