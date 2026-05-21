#ifndef BLE_HOST_H
#define BLE_HOST_H

#include <stdint.h>

extern const char device_name[];
extern const char device_name_short[];

int ble_init();
void ble_task(void *param);

int ble_work_queue_push_task(void (*cb)(void *), void *arg);

#endif  // BLE_HOST_H
