#ifndef BLE_GATT_H
#define BLE_GATT_H

#include <stdint.h>

int ble_gatt_task_prepare(void);
int ble_gatt_notify_chr(uint16_t chr_handle, const uint8_t *payload, uint16_t payload_len);

#endif  // BLE_GATT_H
