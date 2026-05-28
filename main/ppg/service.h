#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include <stdint.h>
#include <host/ble_gatt.h>
#include <host/ble_uuid.h>

int hr_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg);

void ble_gatt_hr_chr_update(void);

#endif // BLE_SERVICE_H
