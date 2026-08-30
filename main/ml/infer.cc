#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <sdkconfig.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>

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
#include "ml/contract.h"
#include "ml/features.h"
#include "ml/service.h"
#include "ml/infer.h"
#include "utils/ecdsa_utils.h"
#include "utils/factory_data.h"

static const char tag[] = APP_TAG "-ml-infer";

#if ENABLE_INSTRUMENTATION
#define INSTR_REPORT_WINDOWS 16

static uint32_t instr_windows;
static int64_t instr_extract_total_us, instr_extract_max_us;
static int64_t instr_invoke_total_us, instr_invoke_max_us;

static void instr_record(int64_t extract_us, int64_t invoke_us) {
  instr_windows++;
  instr_extract_total_us += extract_us;
  instr_invoke_total_us += invoke_us;
  if (extract_us > instr_extract_max_us) instr_extract_max_us = extract_us;
  if (invoke_us > instr_invoke_max_us) instr_invoke_max_us = invoke_us;

  if (instr_windows < INSTR_REPORT_WINDOWS) return;

  ESP_LOGI(tag, "instrumentation over %u windows: extract mean %.2f ms max %.2f ms | "
                "invoke mean %.2f ms max %.2f ms",
           (unsigned)instr_windows,
           (double)instr_extract_total_us / instr_windows / 1000.0,
           (double)instr_extract_max_us / 1000.0,
           (double)instr_invoke_total_us / instr_windows / 1000.0,
           (double)instr_invoke_max_us / 1000.0);

  instr_windows = 0;
  instr_extract_total_us = instr_extract_max_us = 0;
  instr_invoke_total_us = instr_invoke_max_us = 0;
}
#endif

static float feat_mean[ML_N_FEATURES];
static float feat_std[ML_N_FEATURES];

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

#if ENABLE_INSTRUMENTATION
  ESP_LOGI(tag, "instrumentation: tflm arena %d of %d bytes used",
           (int)interpreter->arena_used_bytes(), (int)tensor_arena_size);
#endif

  return ML_ERR_NONE;
}

static int8_t quantize(float value, const TfLiteTensor *tensor) {
  float   scaled    = value / tensor->params.scale;
  int32_t quantized = (int32_t)lroundf(scaled) + tensor->params.zero_point;
  if (quantized > INT8_MAX) quantized = INT8_MAX;
  if (quantized < INT8_MIN) quantized = INT8_MIN;
  return (int8_t)quantized;
}

static int load_server_pubkey(uint8_t pub[ECDSA_P256_PUBKEY_LENGTH]) {
  size_t len = ECDSA_P256_PUBKEY_LENGTH;
  return factory_data_get_blob("srv_pub", pub, &len);
}

static ml_error_code parse_payload(const uint8_t *data, size_t size,
                                   const uint8_t **tflite_out) {
  if (data == NULL || size < 2) return ML_ERR_PAYLOAD;

  uint16_t sig_len;
  memcpy(&sig_len, data, sizeof(sig_len));
  size_t norm_off = 2 + (size_t)sig_len;
  if (norm_off + 2 > size) return ML_ERR_PAYLOAD;

  uint16_t norm_len;
  memcpy(&norm_len, data + norm_off, sizeof(norm_len));
  if (norm_len != ml_contract_norm_len()) {
    ESP_LOGE(tag, "payload carries %u norm bytes, contract wants %u",
             (unsigned)norm_len, (unsigned)ml_contract_norm_len());
    return ML_ERR_PAYLOAD;
  }

  const uint8_t *sig  = data + 2;
  const uint8_t *norm = data + norm_off + 2;
  const uint8_t *body = norm + norm_len;
  if ((size_t)(body - data) + 2 > size) return ML_ERR_PAYLOAD;  // contract must fit
  size_t body_len = size - (size_t)(body - data);

  uint8_t pub[ECDSA_P256_PUBKEY_LENGTH];
  if (load_server_pubkey(pub) != ESP_OK) return ML_ERR_PAYLOAD;
  if (ecdsa_verify(pub, body, body_len, sig, sig_len) != 0) {
    ESP_LOGE(tag, "model payload signature invalid");
    return ML_ERR_PAYLOAD;
  }

  uint16_t contract_version;
  memcpy(&contract_version, body, sizeof(contract_version));
  if (!ml_contract_supported(contract_version)) {
    ESP_LOGE(tag, "unsupported contract version %u", contract_version);
    return ML_ERR_PAYLOAD;
  }

  memcpy(feat_mean, norm, ML_N_FEATURES * sizeof(float));
  memcpy(feat_std, norm + ML_N_FEATURES * sizeof(float), ML_N_FEATURES * sizeof(float));
  *tflite_out = body + 2;
  return ML_ERR_NONE;
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

      const uint8_t *tflite = NULL;
      ml_error_code perr = parse_payload(ml_model_buffer.data, ml_model_buffer.size, &tflite);
      if (perr != ML_ERR_NONE) {
        ESP_LOGE(tag, "model payload rejected with code %d", perr);
        ml_error_notify_send(perr);
        ble_client_invalidate(&ml_model_buffer);
        continue;
      }

      ml_error_code err = ml_build_interpreter(tflite);

      if (err != ML_ERR_NONE || interpreter == NULL) {
        ESP_LOGE(tag, "interpreter failed to build with code %d", err);
        ml_error_notify_send(err);

        ble_client_invalidate(&ml_model_buffer);
        continue;
      }

      int batch_size = input_tensor->dims->data[0];
      int n_features = input_tensor->dims->data[1];

      if (!ml_contract_shape_valid(batch_size, n_features)) {
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

#if ENABLE_INSTRUMENTATION
    int64_t extract_start_us = esp_timer_get_time();
#endif
    ml_extract_features(slice, features);
#if ENABLE_INSTRUMENTATION
    int64_t extract_us = esp_timer_get_time() - extract_start_us;
#endif
    ppg_ring_release_read();
    ml_normalize_features(features, norm_features, feat_mean, feat_std);

    ESP_LOGD(tag, "finished feature extraction on sample %d", sequence_n);

    for (int j = 0; j < ML_N_FEATURES; j++) {
      int8_t v = quantize(norm_features[j], input_tensor);
      input_tensor->data.int8[j] = v;
    }

    ESP_LOGD(tag, "performing inference on sample %d", sequence_n);

#if ENABLE_INSTRUMENTATION
    int64_t invoke_start_us = esp_timer_get_time();
#endif
    TfLiteStatus status = interpreter->Invoke();
#if ENABLE_INSTRUMENTATION
    instr_record(extract_us, esp_timer_get_time() - invoke_start_us);
#endif
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
