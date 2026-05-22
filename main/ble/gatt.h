#ifndef BLE_GATT_H
#define BLE_GATT_H

#include <stdint.h>
#include <host/ble_gatt.h>
#include <host/ble_uuid.h>

extern const ble_uuid16_t hr_svc_uuid;
extern struct ble_gatt_buffer_service ml_buffer_service;

int ble_gatt_init(void);

void ble_gatt_hr_chr_update(void);

void ble_gatt_att_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);

#endif  // BLE_GATT_H
