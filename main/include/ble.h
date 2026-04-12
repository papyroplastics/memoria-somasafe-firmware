#ifndef BLE_H
#define BLE_H

#include <host/ble_uuid.h>

extern const ble_uuid16_t heart_rate_svc_uuid;
extern const ble_uuid16_t heart_rate_chr_uuid;

int ble_init();
void ble_task(void *param);

#endif // BLE_H
