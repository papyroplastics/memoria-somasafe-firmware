#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <sdkconfig.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>

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
#include "utils/ecdsa_utils.h"

static const char tag[] = APP_TAG "-ml-infer";

#define FACTORY_PARTITION "factory_data"
#define FACTORY_NAMESPACE "factory"
#define ML_PAYLOAD_VERSION 1
#define ML_SIGNATURE_VERSION 1

// Norm params from the current model's signed payload, held across the inference
// loop alongside the interpreter they belong to. The device z-scores features with
// these before quantizing, since the int8 model takes already-normalized input.
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
  int err = nvs_flash_init_partition(FACTORY_PARTITION);
  if (err != ESP_OK) {
    ESP_LOGE(tag, "factory nvs init: %s", esp_err_to_name(err));
    return err;
  }
  nvs_handle_t handle;
  err = nvs_open_from_partition(FACTORY_PARTITION, FACTORY_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(tag, "factory nvs open: %s", esp_err_to_name(err));
    return err;
  }
  size_t len = ECDSA_P256_PUBKEY_LENGTH;
  err = nvs_get_blob(handle, "srv_pub", pub, &len);
  nvs_close(handle);
  if (err != ESP_OK) ESP_LOGE(tag, "read srv_pub: %s", esp_err_to_name(err));
  return err;
}

// Verify the signed model payload and locate its embedded tflite, loading the norm
// params into feat_mean/feat_std. Layout (LE): u16 sig_len | sig[sig_len] |
// u16 payload_ver | u16 sig_ver | f32 mean[N] | f32 std[N] | tflite. The ECDSA
// P-256 / SHA-256 signature covers everything after it (versions .. tflite).
static ml_error_code parse_payload(const uint8_t *data, size_t size,
                                   const uint8_t **tflite_out) {
  const size_t norm_len = 2 * ML_N_FEATURES * sizeof(float);
  if (data == NULL || size < 2) return ML_ERR_PAYLOAD;

  uint16_t sig_len;
  memcpy(&sig_len, data, sizeof(sig_len));
  size_t header = 2 + (size_t)sig_len;
  if (header + 4 + norm_len > size) return ML_ERR_PAYLOAD;   // versions + norm must fit

  const uint8_t *sig  = data + 2;
  const uint8_t *body = data + header;
  size_t body_len = size - header;

  uint8_t pub[ECDSA_P256_PUBKEY_LENGTH];
  if (load_server_pubkey(pub) != ESP_OK) return ML_ERR_PAYLOAD;
  if (ecdsa_verify(pub, body, body_len, sig, sig_len) != 0) {
    ESP_LOGE(tag, "model payload signature invalid");
    return ML_ERR_PAYLOAD;
  }

  uint16_t payload_ver, sig_ver;
  memcpy(&payload_ver, body, sizeof(payload_ver));
  memcpy(&sig_ver, body + 2, sizeof(sig_ver));
  if (payload_ver != ML_PAYLOAD_VERSION || sig_ver != ML_SIGNATURE_VERSION) {
    ESP_LOGE(tag, "unsupported payload/signature version %u/%u", payload_ver, sig_ver);
    return ML_ERR_PAYLOAD;
  }

  memcpy(feat_mean, body + 4, ML_N_FEATURES * sizeof(float));
  memcpy(feat_std,  body + 4 + ML_N_FEATURES * sizeof(float), ML_N_FEATURES * sizeof(float));
  *tflite_out = body + 4 + norm_len;
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
    ml_normalize_features(features, norm_features, feat_mean, feat_std);

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
