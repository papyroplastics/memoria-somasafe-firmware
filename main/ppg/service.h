#ifndef PPG_SERVICE_H
#define PPG_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "utils/notif_transaction.h"

#ifdef __cplusplus
extern "C" {
#endif

// Send one second of raw PPG and ACC data as BLE notifications. The whole
// 8-second window is one reconstruction-layer transaction; `tx` is owned by the
// caller and reused across every second of the window (and across windows).
// window_end: true only for the last call of each window, closing the transaction.
// sequence_n: slice sequence number, carried in the service header on the first call.
void ppg_data_notify_send(struct transaction_state *tx,
                     const float *ppg, uint16_t ppg_count,
                     const float *acc, uint16_t acc_count,
                     bool window_end, uint32_t sequence_n);

#ifdef __cplusplus
}
#endif

#endif // PPG_SERVICE_H
