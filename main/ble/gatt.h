#ifndef BLE_GATT_H
#define BLE_GATT_H

#include <stdint.h>
#include <host/ble_uuid.h>
#include <host/ble_gatt.h>

#include "ble/host.h"

extern uint8_t ppg_data_chr_notify;
extern uint16_t ppg_data_chr_handle;

extern uint8_t ml_result_chr_notify;
extern uint8_t ml_errors_chr_notify;
extern uint16_t ml_result_chr_handle;
extern uint16_t ml_errors_chr_handle;

extern const ble_uuid128_t svc_uuid128[];
extern const uint8_t       svc_uuid128_cnt;

int ble_gatt_task_prepare(void);
int ble_gatt_notify_chr(uint16_t chr_handle, const uint8_t *payload, uint16_t payload_len);
void ble_gatt_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
void ble_gatt_subscribe_cb(uint16_t attr_handle, uint8_t notify);

#if SMP_SECURITY_LEVEL == 0
#define GATT_CHR_READ_FLAGS   BLE_GATT_CHR_F_READ
#define GATT_CHR_WRITE_FLAGS  BLE_GATT_CHR_F_WRITE
#define GATT_CHR_NOTIFY_FLAGS BLE_GATT_CHR_F_NOTIFY
#define GATT_DSC_READ_FLAGS   BLE_ATT_F_READ
#define GATT_DSC_WRITE_FLAGS  BLE_ATT_F_WRITE

#elif SMP_SECURITY_LEVEL == 1
#define GATT_CHR_READ_ENC \
        BLE_GATT_CHR_F_READ \
      | BLE_GATT_CHR_F_READ_ENC 

#define GATT_CHR_WRITE_ENC \
        BLE_GATT_CHR_F_WRITE \
      | BLE_GATT_CHR_F_WRITE_ENC

#define GATT_CHR_NOTIFY_ENC \
      | BLE_GATT_CHR_F_NOTIFY 
      | BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC

#define GATT_DSC_READ_ENC \
        BLE_ATT_F_READ \
      | BLE_ATT_F_READ_ENC

#define GATT_DSC_WRITE_ENC \
        BLE_ATT_F_WRITE \
      | BLE_ATT_F_WRITE_ENC

#elif SMP_SECURITY_LEVEL == 2 || SMP_SECURITY_LEVEL == 3
#define GATT_CHR_READ_ENC \
        BLE_GATT_CHR_F_READ \
      | BLE_GATT_CHR_F_READ_ENC \
      | BLE_GATT_CHR_F_READ_AUTHEN

#define GATT_CHR_WRITE_ENC \
        BLE_GATT_CHR_F_WRITE \
      | BLE_GATT_CHR_F_WRITE_ENC \
      | BLE_GATT_CHR_F_WRITE_AUTHEN

#define GATT_CHR_NOTIFY_ENC \
        BLE_GATT_CHR_F_NOTIFY \
      | BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC \
      | BLE_GATT_CHR_F_NOTIFY_INDICATE_AUTHEN

#define GATT_DSC_READ_ENC \
        BLE_ATT_F_READ \
      | BLE_ATT_F_READ_ENC \
      | BLE_ATT_F_READ_AUTHEN

#define GATT_DSC_WRITE_ENC \
        BLE_ATT_F_WRITE \
      | BLE_ATT_F_WRITE_ENC \
      | BLE_ATT_F_WRITE_AUTHEN

#else
#error "security level must be 0-3"
#endif

#endif  // BLE_GATT_H
