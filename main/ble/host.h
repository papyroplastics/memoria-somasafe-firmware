#ifndef BLE_HOST_H
#define BLE_HOST_H

#include <host/ble_uuid.h>

extern const char device_name[];
extern const char device_name_short[];
extern const uint16_t device_appearance;

extern const ble_uuid16_t hr_svc_uuid;
extern const ble_uuid16_t hr_chr_uuid;

extern const ble_uuid128_t model_svc_uuid;
extern const ble_uuid128_t model_chr_uuid;
extern const ble_uuid128_t model_size_dsc_uuid;
extern const ble_uuid128_t model_pos_dsc_uuid;
extern const ble_uuid128_t model_sha_dsc_uuid;

int ble_init();
void ble_task(void *param);

int push_work_to_nimple_host_task(void (*cb)(void *), void *arg);

#endif  // BLE_HOST_H
