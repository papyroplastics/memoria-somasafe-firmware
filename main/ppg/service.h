#ifndef PPG_SERVICE_H
#define PPG_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Send one second of raw PPG and ACC data as BLE notifications.
// window_start: true only for the first call of each 8-second window.
// sequence_n: slice sequence number, sent in the window-start header.
void ppg_notify_data(const float *ppg, uint16_t ppg_count,
                     const float *acc, uint16_t acc_count,
                     bool window_start, uint32_t sequence_n);

#ifdef __cplusplus
}
#endif

#endif  // PPG_SERVICE_H
