#ifndef BLE_HOST_H
#define BLE_HOST_H

#include <stddef.h>
#include <stdint.h>

extern const char device_name[];
extern const uint8_t device_name_len;
extern const char device_short_name[];
extern const uint8_t device_short_name_lenght;

int ble_init();
void ble_task(void *param);

int ble_work_queue_push_task(void (*cb)(void *), void *arg);

#endif  // BLE_HOST_H
