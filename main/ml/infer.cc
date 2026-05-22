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
#include "ble/buffer_defs.h"
#include "ml/infer.h"
#include "ppg/sensor.h"

static const char tag[] = APP_TAG "-infer";

extern "C" const ble_uuid128_t ml_results_chr_uuid = BLE_UUID128_INIT(
    0x54, 0x3c, 0xc2, 0x5a, 0x71, 0x1f, 0x4d, 0xfa,
    0x9c, 0x4b, 0xc1, 0x4f, 0x86, 0xd0, 0x28, 0x72,
);
extern "C" const ble_uuid128_t ml_errors_chr_uuid = BLE_UUID128_INIT(
    0x0e, 0x33, 0x1f, 0xcf, 0x7a, 0x8a, 0x42, 0xc6,
    0xa5, 0x6e, 0x25, 0x5a, 0x42, 0x8c, 0x8b, 0x9c,
);

extern "C" uint16_t ml_results_chr_handle;
extern "C" uint16_t ml_errors_chr_handle;

static const uint16_t tensor_arena_size = 4096;
static uint8_t tensor_arena[tensor_arena_size];

static tflite::MicroInterpreter *interpreter;
static TfLiteTensor *input_tensor;
static TfLiteTensor *output_tensor;
static bool resolver_ready;
static tflite::MicroMutableOpResolver<2> resolver;

static uint16_t ml_results_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t ml_errors_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t ml_results_mtu = BLE_ATT_MTU_DFLT;

#define ML_RESULTS_MAX_PAYLOAD 256
static uint8_t ml_results_payload[ML_RESULTS_MAX_PAYLOAD];
static uint16_t ml_results_len;

enum ml_error_code {
  ML_ERR_NONE = 0,
  ML_ERR_MODEL_LOAD = 1,
  ML_ERR_UNSUPPORTED_OP = 2,
  ML_ERR_TENSOR_ALLOC = 3,
  ML_ERR_INVOKE = 4,
  ML_ERR_INVALID_SHAPE = 5,
};

static uint8_t ml_last_error = ML_ERR_NONE;

static void ml_notify_payload(uint16_t conn_handle, uint16_t chr_handle,
    const uint8_t *payload, uint16_t payload_len) {
  if (conn_handle == BLE_HS_CONN_HANDLE_NONE || payload_len == 0) {
    return;
  }

  struct os_mbuf *om = ble_hs_mbuf_from_flat(payload, payload_len);
  if (om == NULL) {
    ESP_LOGE(tag, "failed to allocate notify buffer");
    return;
  }

  int err = ble_gatts_notify_custom(conn_handle, chr_handle, om);
  if (err != 0) {
    ESP_LOGE(tag, "notify failed with err %d", err);
  }
}

static void ml_report_error(enum ml_error_code code) {
  ml_last_error = (uint8_t)code;

  uint16_t conn_handle = ml_errors_conn_handle != BLE_HS_CONN_HANDLE_NONE
      ? ml_errors_conn_handle
      : ml_results_conn_handle;
  ml_notify_payload(conn_handle, ml_errors_chr_handle,
      &ml_last_error, sizeof(ml_last_error));
}

static int ml_build_interpreter(const uint8_t *model_data) {
  if (model_data == NULL) {
    ESP_LOGE(tag, "model buffer is empty");
    return ML_ERR_MODEL_LOAD;
  }

  const tflite::Model *model = tflite::GetModel(model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    ESP_LOGE(tag, "unsupported model schema version %d", model->version());
    return ML_ERR_MODEL_LOAD;
  }

  if (!resolver_ready) {
    if (resolver.AddFullyConnected() != kTfLiteOk) {
      return ML_ERR_UNSUPPORTED_OP;
    }
    if (resolver.AddTanh() != kTfLiteOk) {
      return ML_ERR_UNSUPPORTED_OP;
    }
    resolver_ready = true;
  }

  delete interpreter;
  interpreter = new tflite::MicroInterpreter(model, resolver, tensor_arena, tensor_arena_size);
  if (interpreter == NULL) {
    return ML_ERR_TENSOR_ALLOC;
  }

  TfLiteStatus allocate_status = interpreter->AllocateTensors();
  if (allocate_status != kTfLiteOk) {
    ESP_LOGE(tag, "model interpreter tensor allocation failed");
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

static void ml_send_results(const int8_t *inputs, const int8_t *outputs, size_t count) {
  uint16_t mtu = ml_results_mtu;
  if (mtu < BLE_ATT_MTU_DFLT) {
    mtu = BLE_ATT_MTU_DFLT;
  }

  uint16_t max_values = (uint16_t)(mtu - 3);
  if (max_values % 2 != 0) {
    max_values--;
  }

  size_t max_pairs = max_values / 2;
  if (max_pairs == 0) {
    return;
  }

  size_t offset = 0;
  while (offset < count) {
    size_t pairs = count - offset;
    if (pairs > max_pairs) {
      pairs = max_pairs;
    }

    size_t payload_len = pairs * 2;
    if (payload_len > ML_RESULTS_MAX_PAYLOAD) {
      payload_len = ML_RESULTS_MAX_PAYLOAD;
      pairs = payload_len / 2;
    }

    memcpy(ml_results_payload, inputs + offset, pairs);
    memcpy(ml_results_payload + pairs, outputs + offset, pairs);
    ml_results_len = (uint16_t)(pairs * 2);

    ml_notify_payload(ml_results_conn_handle, ml_results_chr_handle,
        ml_results_payload, ml_results_len);

    offset += pairs;
  }
}

int ml_results_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)attr_handle;
  (void)arg;

  if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
  }

  uint16_t mtu = ble_att_mtu(conn_handle);
  if (mtu != 0) {
    ml_results_mtu = mtu;
  }
  ml_results_conn_handle = conn_handle;

  if (ml_results_len == 0) {
    return 0;
  }

  if (os_mbuf_append(ctxt->om, ml_results_payload, ml_results_len) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  return 0;
}

int ml_errors_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)attr_handle;
  (void)arg;

  if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
  }

  ml_errors_conn_handle = conn_handle;

  if (os_mbuf_append(ctxt->om, &ml_last_error, sizeof(ml_last_error)) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  return 0;
}

void ml_task(void *param) {
  (void)param;

  int8_t quantized[PPG_SNAPSHOT_SAMPLES];

  for (;;) {
    bool dirty = ble_client_buffer_lock(&ml_buffer_service.buffer);
    if (ml_buffer_service.buffer.data == NULL || ml_buffer_service.buffer.size == 0) {
      ble_client_buffer_unlock(&ml_buffer_service.buffer);
      ml_report_error(ML_ERR_MODEL_LOAD);
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    if (dirty || interpreter == NULL) {
      int err = ml_build_interpreter(ml_buffer_service.buffer.data);
      if (err != ML_ERR_NONE) {
        ble_client_buffer_unlock(&ml_buffer_service.buffer);
        ml_report_error((enum ml_error_code)err);
        vTaskDelay(pdMS_TO_TICKS(500));
        continue;
      }
    }

    struct ppg_snapshot *snapshot = NULL;
    if (!ppg_ring_acquire_read(&snapshot)) {
      ble_client_buffer_unlock(&ml_buffer_service.buffer);
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    size_t sample_count = snapshot->sample_count;
    if (sample_count > PPG_SNAPSHOT_SAMPLES) {
      sample_count = PPG_SNAPSHOT_SAMPLES;
    }

    for (size_t i = 0; i < sample_count; i++) {
      quantized[i] = quantize_value(snapshot->samples[i], input_tensor);
    }
    ppg_ring_release_read();

    if (sample_count == 0) {
      ble_client_buffer_unlock(&ml_buffer_service.buffer);
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    int batch_size = input_tensor->dims->data[0];
    if (batch_size <= 0 || output_tensor->dims->data[0] != batch_size) {
      ble_client_buffer_unlock(&ml_buffer_service.buffer);
      ml_report_error(ML_ERR_INVALID_SHAPE);
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    size_t offset = 0;
    while (offset < sample_count) {
      size_t batch = sample_count - offset;
      if ((int)batch > batch_size) {
        batch = (size_t)batch_size;
      }

      for (int i = 0; i < batch_size; i++) {
        int8_t value = input_tensor->params.zero_point;
        if ((size_t)i < batch) {
          value = quantized[offset + (size_t)i];
        }
        input_tensor->data.int8[i] = value;
      }

      TfLiteStatus invoke_status = interpreter->Invoke();
      if (invoke_status != kTfLiteOk) {
        ble_client_buffer_unlock(&ml_buffer_service.buffer);
        ml_report_error(ML_ERR_INVOKE);
        break;
      }

      static int8_t outputs[PPG_SNAPSHOT_SAMPLES];
      for (size_t i = 0; i < batch; i++) {
        outputs[i] = output_tensor->data.int8[i];
      }

      ble_client_buffer_unlock(&ml_buffer_service.buffer);
      ml_send_results(quantized + offset, outputs, batch);

      offset += batch;
      if (offset < sample_count) {
        dirty = ble_client_buffer_lock(&ml_buffer_service.buffer);
        if (dirty) {
          int err = ml_build_interpreter(ml_buffer_service.buffer.data);
          if (err != ML_ERR_NONE) {
            ble_client_buffer_unlock(&ml_buffer_service.buffer);
            ml_report_error((enum ml_error_code)err);
            break;
          }
        }
      }
    }
  }
}
