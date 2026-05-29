#ifndef ML_SERVICE_H
#define ML_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <host/ble_gatt.h>

enum ml_error_code {
  ML_ERR_NONE = 0,
  ML_ERR_MODEL_LOAD = 1,
  ML_ERR_UNSUPPORTED_OP = 2,
  ML_ERR_TENSOR_ALLOC = 3,
  ML_ERR_INVOKE = 4,
  ML_ERR_INVALID_SHAPE = 5,
};

extern struct ble_client_buffer ml_model_buffer;

int ml_errors_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg);

void ml_report_error(enum ml_error_code code);
void ml_send_slice_start(uint32_t start_ms, uint32_t end_ms);
void ml_send_results(const int8_t *inputs, const int8_t *outputs, size_t count);

#ifdef __cplusplus
}
#endif

#endif // ML_SERVICE_H
