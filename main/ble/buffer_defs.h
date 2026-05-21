#ifndef BLE_BUFFER_DEFS_H
#define BLE_BUFFER_DEFS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <host/ble_uuid.h>

#include "ble/client_buffer.h"

extern struct ble_gatt_buffer_service model_buffer_service;

extern struct ble_gatt_buffer_service *gatt_buffer_services[];
extern const size_t gatt_buffer_services_len;

bool ble_buffer_defs_register(
    const ble_uuid_t *svc_uuid, const ble_uuid_t *chr_uuid,
    const ble_uuid_t *dsc_uuid, uint16_t dsc_handle
);

#endif // BLE_BUFFER_DEFS_H
