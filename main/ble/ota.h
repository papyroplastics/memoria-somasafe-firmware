#ifndef BLE_OTA_H
#define BLE_OTA_H

#include <stdint.h>

#include <host/ble_gatt.h>

#ifdef __cplusplus
extern "C" {
#endif

// OTA update state machine, driven by the client through the state
// characteristic (the phone is authoritative; the device performs no version
// checks). IDLE -> client writes RECEIVING: an update begins and the image and
// signature characteristics accept sequential writes. Client writes VERIFYING:
// the image's ECDSA P-256/SHA-256 signature is checked against the factory
// srv_pub and the image is validated; on success the new boot partition is set,
// VERIFYING is notified and the device restarts shortly after. Any failure
// aborts the update and notifies ERROR. Writing RECEIVING again (from any state
// but VERIFYING) aborts and restarts the transfer; writing IDLE aborts it.
enum ble_ota_state {
  BLE_OTA_STATE_IDLE = 0,
  BLE_OTA_STATE_RECEIVING = 1,
  BLE_OTA_STATE_VERIFYING = 2,
  BLE_OTA_STATE_ERROR = 0xFF,
};

int ble_ota_version_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg);
int ble_ota_data_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg);
int ble_ota_state_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg);
int ble_ota_signature_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif // BLE_OTA_H
