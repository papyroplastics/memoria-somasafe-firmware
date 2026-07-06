#ifndef BLE_HOST_H
#define BLE_HOST_H

#include <stddef.h>
#include <stdint.h>

// Use legacy advertising with the extended adv API
#define EXT_ADV_LEGACY_PDU 1
#define SMP_SECURITY_LEVEL 0

// Version of the whole app<->firmware BLE contract: every service framing
// (client buffer, reconstruction layer, PPG/ML/device services) and the model
// payload layout the app assembles. Registered server-side per firmware build
// (Firmware.interface_version); bump when any of those change shape. The
// per-model feeding contract is versioned separately (ml/contract.h).
#define BLE_INTERFACE_VERSION 1

extern const char device_name[];
extern const uint8_t device_name_len;
extern const char device_short_name[];
extern const uint8_t device_short_name_len;

int ble_init();
void ble_task(void *param);

int ble_work_queue_push_task(void (*cb)(void *), void *arg);

#endif // BLE_HOST_H
