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

// Notify one inference result: the quantized input feature vector followed by
// the model's score output tensor, split across packets per the MTU.
// The first packet starts with a 1 followed by the slice sequence number;
// continuation packets start with a 0.
void ml_notify_result(uint32_t sequence_n,
                      const int8_t *features, size_t features_len,
                      const int8_t *score, size_t score_len);

#ifdef __cplusplus
}
#endif

#endif // ML_SERVICE_H
