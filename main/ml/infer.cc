#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <esp_log.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/micro/micro_mutable_op_resolver.h>

#include "common.h"
#include "ble/gatt.h"
#include "ble/client_buffer.h"
#include "ml/infer.h"

static const char tag[] = APP_TAG "-infer";

struct ble_client_buffer model_buffer = model_buffer_service.buffer;
static const uint16_t tensor_arena_size = 1024;
uint8_t tensor_arena[tensor_arena_size];

enum ml_infer_err {
  ML_INFER_ERR_NONE = 0,
  MALFORMED_MODEL,
  UNSUPPORTED_OPPERATION,
  TENSOR_ALLOC_ERROR,
  INFERENCE_ERROR,
};

bool buf_ready = false;

enum ml_infer_err run_inference() {
  const tflite::Model* model = tflite::GetModel(model_buffer.data);

  if (model->version() != TFLITE_SCHEMA_VERSION) {
    ESP_LOGE(tag, "Model provided is schema version %d not equal to supported version %d.",
        model->version(), TFLITE_SCHEMA_VERSION);
    return MALFORMED_MODEL;
  }

  tflite::MicroMutableOpResolver<2> resolver;
  if (resolver.AddFullyConnected() != kTfLiteOk) return UNSUPPORTED_OPPERATION;
  if (resolver.AddTanh() != kTfLiteOk) return UNSUPPORTED_OPPERATION;

  tflite::MicroInterpreter interpreter(model, resolver, tensor_arena, tensor_arena_size);

  TfLiteStatus allocate_status = interpreter.AllocateTensors();
  if (allocate_status != kTfLiteOk) {
    ESP_LOGE(tag, "model interpreter tensor allocation failed");
    return TENSOR_ALLOC_ERROR;
  }

  TfLiteTensor* input = interpreter.input(0);
  TfLiteTensor* output = interpreter.output(0);

  int batch_size = input->dims->data[0];
  const float data_max = 2 * M_PI;
  const float data_step = data_max / batch_size;

  for (int i = 0; i < batch_size; i++) {
    float x = i * data_step;
    input->data.int8[i] = x / input->params.scale + input->params.zero_point;;
  }

  TfLiteStatus invoke_status = interpreter.Invoke();
  if (invoke_status != kTfLiteOk) {
    ESP_LOGE(tag, "model interpreter invocation failed");
    return INFERENCE_ERROR;
  }


  for (int i = 0; i < batch_size; i++) {
    float x = i * data_step;
    int8_t y_quantized = output->data.int8[0];
    float y = (y_quantized - output->params.zero_point) * output->params.scale;

    printf("%f,%f", x, y);
  }

  return ML_INFER_ERR_NONE;
}


