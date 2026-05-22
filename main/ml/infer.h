#ifndef ML_INFER_H
#define ML_INFER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <host/ble_gatt.h>
#include <host/ble_uuid.h>

extern const ble_uuid128_t ml_results_chr_uuid;
extern const ble_uuid128_t ml_errors_chr_uuid;

extern uint16_t ml_results_chr_handle;
extern uint16_t ml_errors_chr_handle;

int ml_results_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg);
int ml_errors_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg);

void ml_task(void *param);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ML_INFER_H
