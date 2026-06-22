#ifndef DEVICE_SERVICE_H
#define DEVICE_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <host/ble_gatt.h>

#include "ble/client_buffer.h"

// Client buffer holding the arbitrary-length payload the client wants signed.
extern struct ble_client_buffer device_sign_buffer;

// Read-only access callback returning the factory-provisioned serial number.
int device_serial_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg);

// Signing task: blocks until the sign buffer is readied, signs its contents with
// the device ECDSA private key (factory NVS), resets the buffer to NOT_READY and
// notifies the DER signature back as a reconstruction-layer transaction (see
// utils/notif_transaction.h).
void device_sign_task(void *param);

#ifdef __cplusplus
}
#endif

#endif // DEVICE_SERVICE_H
