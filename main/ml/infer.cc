#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <sdkconfig.h>
#include <esp_log.h>
#include <esp_system.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <portmacro.h>

#include <host/ble_att.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>

#include <tensorflow/lite/c/c_api_types.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/micro/micro_mutable_op_resolver.h>

#include "common.h"
#include "ble/client_buffer.h"
#include "ppg/sensor.h"
#include "ml/features.h"
#include "ml/service.h"
#include "ml/infer.h"
#include "ml/norm_params.h"

static const char tag[] = APP_TAG "-ml-infer";

static tflite::MicroMutableOpResolver<2> resolver;

static const uint16_t tensor_arena_size = 1024 * 16;
static __attribute__((aligned(16))) uint8_t tensor_arena[tensor_arena_size];

static const tflite::Model *model;
static tflite::MicroInterpreter *interpreter;
static TfLiteTensor *input_tensor;
static TfLiteTensor *score_tensor;

static void unset_interpreter() {
  if (interpreter != NULL) delete interpreter;
  interpreter = NULL;
  model = NULL;
  input_tensor = NULL;
  score_tensor = NULL;
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

  if (interpreter->AllocateTensors() != kTfLiteOk) {
    ESP_LOGE(tag, "tensor allocation failed");
    unset_interpreter();
    return ML_ERR_TENSOR_ALLOC;
  }

  if (interpreter->outputs_size() != 1) {
    ESP_LOGE(tag, "expected 1 output tensor (score), got %d",
             (int)interpreter->outputs_size());
    unset_interpreter();
    return ML_ERR_INVALID_SHAPE;
  }

  input_tensor = interpreter->input(0);
  score_tensor = interpreter->output(0);
  return ML_ERR_NONE;
}

static int8_t quantize(float value, const TfLiteTensor *tensor) {
  float   scaled    = value / tensor->params.scale;
  int32_t quantized = (int32_t)lroundf(scaled) + tensor->params.zero_point;
  if (quantized > INT8_MAX) quantized = INT8_MAX;
  if (quantized < INT8_MIN) quantized = INT8_MIN;
  return (int8_t)quantized;
}

void ml_task(void *param) {
  (void)param;

  if (resolver.AddFullyConnected() != kTfLiteOk 
      || resolver.AddRelu() != kTfLiteOk
  ) {
    ESP_LOGE(tag, "required tflite op not supported");
    esp_restart();
  }

  ml_features_init();

  static float features[ML_BATCH_SIZE * ML_N_FEATURES];
  static float norm_features[ML_BATCH_SIZE * ML_N_FEATURES];

  bool model_invalid = true;

  for (;;) {
    ESP_LOGD(tag, "starting inference interation");

    bool dirty = ble_client_buffer_lock(&ml_model_buffer);
    ESP_LOGI(tag, "locked %s model buffer", dirty ? "dirty" : "clean");

    if (dirty) {
      model_invalid = true;
      ml_error_code err = ml_build_interpreter(ml_model_buffer.data);

      if (err != ML_ERR_NONE || interpreter == NULL) {
        ESP_LOGE(tag, "interpreter failed to build with code %d", err);
        ml_error_notify_send(err);

        ble_client_invalidate(&ml_model_buffer);
        continue;
      }

      int batch_size = input_tensor->dims->data[0];
      int n_features = input_tensor->dims->data[1];

      if (batch_size != ML_BATCH_SIZE || n_features != ML_N_FEATURES) {
        ESP_LOGE(tag, "invalid model input signature shape: batch_size=%d n_features=%d", 
            batch_size, n_features);
        ml_error_notify_send(ML_ERR_INVALID_SHAPE);

        ble_client_invalidate(&ml_model_buffer);
        continue;
      }

      model_invalid = false;
      ESP_LOGI(tag, "interpreter succesfuly validated");

    } else if (model_invalid) {
      ESP_LOGE(tag, "locked clean invalid model, this branch should never occur");
      ble_client_invalidate(&ml_model_buffer);
      continue;
    }

    ESP_LOGD(tag, "obtaining data sample");
    struct ppg_slice *slice = NULL;
    if (!ppg_ring_acquire_read(&slice)) {
      ESP_LOGI(tag, "ppg ring buffer empty, waiting for data");
      ble_client_buffer_unlock(&ml_model_buffer);
      ppg_ring_wait_data();
      continue;
    }

    uint32_t sequence_n = slice->sequence_n;
    ESP_LOGD(tag, "acquired sample %d from ring buffer", sequence_n);

    ml_extract_features(slice, features);
    ppg_ring_release_read();
    ml_normalize_features(features, norm_features);

    ESP_LOGD(tag, "finished feature extraction on sample %d", sequence_n);

    // Fill input buffer from the normalized copy; `features` stays raw so the
    // phone receives un-normalized features it can reproduce on-device.
    for (int j = 0; j < ML_N_FEATURES; j++) {
      int8_t v = quantize(norm_features[j], input_tensor);
      input_tensor->data.int8[j] = v;
    }

    ESP_LOGD(tag, "performing inference on sample %d", sequence_n);

    TfLiteStatus status = interpreter->Invoke();
    ble_client_buffer_unlock(&ml_model_buffer);

    if (status != kTfLiteOk) {
      ESP_LOGE(tag, "inference returned error status %d", status);
      ml_error_notify_send(ML_ERR_INVOKE);
      continue;
    }

    ESP_LOGI(tag, "finished inference on sample %d", sequence_n);

    ml_result_notify_send(sequence_n,
                     features, sizeof(features),
                     score_tensor->data.int8, score_tensor->bytes);
  }

  ESP_LOGE(tag, "ML task exited unexpectedly");
  esp_restart();
}
