#ifndef BLE_GATT_H
#define BLE_GATT_H

#include <stdint.h>
#include <host/ble_uuid.h>

extern uint16_t hr_chr_handle;

extern uint16_t ml_results_chr_handle;
extern uint16_t ml_errors_chr_handle;

extern const ble_uuid16_t svc_uuid16[];
extern const uint8_t svc_uuid16_cnt;

extern const ble_uuid128_t svc_uuid128[];
extern const uint8_t svc_uuid128_cnt;

int ble_gatt_task_prepare(void);
int ble_gatt_notify_chr(uint16_t chr_handle, const uint8_t *payload, uint16_t payload_len);

#endif  // BLE_GATT_H
