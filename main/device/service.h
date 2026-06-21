#ifndef DEVICE_SERVICE_H
#define DEVICE_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ble/client_buffer.h"

// Client buffer holding the arbitrary-length payload the client wants signed.
extern struct ble_client_buffer device_sign_buffer;

// Signing task: blocks until the sign buffer is readied, signs its contents with
// the device ECDSA private key (factory NVS), resets the buffer to NOT_READY and
// notifies the DER signature back as a reconstruction-layer transaction (see
// utils/notif_transaction.h).
void device_sign_task(void *param);

#ifdef __cplusplus
}
#endif

#endif // DEVICE_SERVICE_H
