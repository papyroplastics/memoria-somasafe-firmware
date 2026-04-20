#ifndef BLE_H
#define BLE_H

#include <host/ble_uuid.h>

extern const ble_uuid16_t hr_svc_uuid;
extern const ble_uuid16_t hr_chr_uuid;

int ble_init();
void ble_task(void *param);

int push_work_to_nimple_host_task(void (*cb)(void *), void *arg);

#endif  // BLE_H
