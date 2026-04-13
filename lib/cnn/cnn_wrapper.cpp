// Обертка cpp-скрипта, позволяющая запустить cpp через c

#include "cnn_wrapper.h"
#include "cnn.h"
#include <iostream>

extern "C" float predict_from_file(char* wav_filename, char* weights_dirname) {
    
    std::string processed_wav_filename(wav_filename);
    std::string processed_weights_dirname(weights_dirname);
    
    // Это дебри специалиста по нейросетям
    return cnn_processing(processed_wav_filename, processed_weights_dirname);

}