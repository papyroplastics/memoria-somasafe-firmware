#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <host/ble_att.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/micro/micro_mutable_op_resolver.h>

#include "common.h"
#include "ble/client_buffer.h"
#include "esp_system.h"
#include "ml/service.h"
#include "ml/infer.h"
#include "ppg/sensor.h"

static const char tag[] = APP_TAG "-ml-infer";

static tflite::MicroMutableOpResolver<2> resolver;

static const uint16_t tensor_arena_size = 16384;
static uint8_t tensor_arena[tensor_arena_size];

static const tflite::Model *model;
static tflite::MicroInterpreter *interpreter;
static TfLiteTensor *input_tensor;
static TfLiteTensor *output_tensor;

void unset_interpreter() {
  if (interpreter != NULL) delete interpreter; 
  interpreter = NULL;
  model = NULL;
  input_tensor = NULL;
  output_tensor = NULL;
}

static ml_error_code ml_build_interpreter(const uint8_t *model_data) {
  if (model_data == NULL) {
    ESP_LOGE(tag, "model buffer is empty");
    return ML_ERR_MODEL_LOAD;
  }

  model = tflite::GetModel(model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    ESP_LOGE(tag, "unsupported model schema version %d", model->version());
    unset_interpreter();
    return ML_ERR_MODEL_LOAD;
  }

  interpreter = new tflite::MicroInterpreter(model, resolver, tensor_arena, tensor_arena_size);
  if (interpreter == NULL) {
    unset_interpreter();
    return ML_ERR_TENSOR_ALLOC;
  }

  TfLiteStatus allocate_status = interpreter->AllocateTensors();
  if (allocate_status != kTfLiteOk) {
    ESP_LOGE(tag, "model interpreter tensor allocation failed");
    unset_interpreter();
    return ML_ERR_TENSOR_ALLOC;
  }

  input_tensor = interpreter->input(0);
  output_tensor = interpreter->output(0);
  return ML_ERR_NONE;
}

static int8_t quantize_value(float value, const TfLiteTensor *tensor) {
  float scaled = value / tensor->params.scale;
  int32_t quantized = (int32_t)lroundf(scaled) + tensor->params.zero_point;
  if (quantized > INT8_MAX) quantized = INT8_MAX;
  if (quantized < INT8_MIN) quantized = INT8_MIN;
  return (int8_t)quantized;
}


void ml_task(void *param) {
  (void)param;

  if (
    resolver.AddFullyConnected() != kTfLiteOk ||
    resolver.AddTanh() != kTfLiteOk
  ) {
    ESP_LOGE(tag, "required tflite operation not suported");
    esp_restart();
  }

  for (;;) {
    bool dirty = ble_client_buffer_lock(&ml_model_buffer);
    if (dirty) {
      enum ml_error_code err = ml_build_interpreter(ml_model_buffer.data);

      if (err != ML_ERR_NONE) {
        ble_client_buffer_unlock(&ml_model_buffer);
        ml_report_error(err);
        vTaskDelay(pdMS_TO_TICKS(500));
        continue;
      }
    }

    if (interpreter == NULL) {
      ble_client_buffer_unlock(&ml_model_buffer);
      vTaskDelay(pdMS_TO_TICKS(500));
    }

    if (ml_model_buffer.data == NULL || ml_model_buffer.size == 0) {
      ble_client_buffer_unlock(&ml_model_buffer);
      ml_report_error(ML_ERR_MODEL_LOAD);
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    struct ppg_slice *slice = NULL;
    if (!ppg_ring_acquire_read(&slice)) {
      ble_client_buffer_unlock(&ml_model_buffer);
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    size_t sample_count = slice->sample_count;

    if (sample_count == 0) {
      ppg_ring_release_read();
      ble_client_buffer_unlock(&ml_model_buffer);
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    int batch_size = input_tensor->dims->data[0];
    if (batch_size <= 0 || output_tensor->dims->data[0] != batch_size) {
      ppg_ring_release_read();
      ble_client_buffer_unlock(&ml_model_buffer);
      ml_report_error(ML_ERR_INVALID_SHAPE);
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    ml_send_slice_start(slice->start_ms, slice->end_ms);

    size_t offset = 0;
    while (offset < sample_count) {
      size_t batch = sample_count - offset;
      if ((int)batch > batch_size) {
        batch = (size_t)batch_size;
      }

      for (int i = 0; i < batch_size; i++) {
        int8_t value = input_tensor->params.zero_point;
        if ((size_t)i < batch) {
          value = quantize_value(slice->samples[offset + (size_t)i], input_tensor);
        }
        input_tensor->data.int8[i] = value;
      }

      TfLiteStatus invoke_status = interpreter->Invoke();
      if (invoke_status != kTfLiteOk) {
        ble_client_buffer_unlock(&ml_model_buffer);
        ml_report_error(ML_ERR_INVOKE);
        break;
      }

      ble_client_buffer_unlock(&ml_model_buffer);
      ml_send_results(input_tensor->data.int8, output_tensor->data.int8, batch);

      offset += batch;
      if (offset < sample_count) {
        dirty = ble_client_buffer_lock(&ml_model_buffer);
        if (dirty) {
          int err = ml_build_interpreter(ml_model_buffer.data);
          if (err != ML_ERR_NONE) {
            ble_client_buffer_unlock(&ml_model_buffer);
            ml_report_error((enum ml_error_code)err);
            break;
          }
        }
      }
    }

    ppg_ring_release_read();
  }

  ESP_LOGE(tag, "ML task exited unexpectedly");
  esp_restart();
}
