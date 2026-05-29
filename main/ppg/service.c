#include <esp_log.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <host/ble_att.h>

#include "common.h"
#include "ble/gatt.h"
#include "ppg/service.h"
#include "ppg/sensor.h"

static const char tag[] = APP_TAG "-ppg-service";

int hr_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)attr_handle;
  (void)arg;

  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR: {
        const uint8_t hr_chr_value[2] = {
          0, // flags
          ppg_get_hr()
        };

        int err = os_mbuf_append(ctxt->om, hr_chr_value, sizeof(hr_chr_value));
        if (err != 0) {
          ESP_LOGE(tag, "unable to answer read on HR chr due to internal error");
          return BLE_ATT_ERR_UNLIKELY;
        }
        return 0;
      }

    default:
      ESP_LOGE(tag, "illegal operation to HR chr with code: %d", ctxt->op);
      return BLE_ATT_ERR_UNLIKELY;
  }
}

