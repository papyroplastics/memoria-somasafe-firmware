#include <stdint.h>
#include <stddef.h>
#include <esp_log.h>
#include <host/ble_hs.h>
#include <host/ble_gatt.h>
#include <host/ble_att.h>
#include <host/ble_uuid.h>
#include <services/gatt/ble_svc_gatt.h>

#include "common.h"
#include "ble/gatt.h"
#include "ble/buffer_defs.h"
#include "ble/client_buffer.h"
#include "ml/infer.h"
#include "ppg/sensor.h"

static const char tag[] = APP_TAG "-gatt";

static uint16_t hr_chr_handle;
const ble_uuid16_t hr_svc_uuid = BLE_UUID16_INIT(0x180D);
static const ble_uuid16_t hr_chr_uuid = BLE_UUID16_INIT(0x2A37);

static int hr_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
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
          ESP_LOGI(tag, "unable to answer read on HR chr due to internal error");
          return BLE_ATT_ERR_UNLIKELY;
        }
        return 0;
      }

    default:
      ESP_LOGE(tag, "illegal operation to HR chr with code: %d", ctxt->op);
      return BLE_ATT_ERR_UNLIKELY;
  }
}

// clang-format off
static struct ble_gatt_svc_def gatt_svcs[] = {
  {
    .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = &hr_svc_uuid.u,
    .includes = NULL,
    .characteristics = (struct ble_gatt_chr_def[]) {
      {
        .uuid = &hr_chr_uuid.u,
        .access_cb = hr_chr_access_cb,
        .arg = NULL,
        .descriptors = NULL,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .min_key_size = 0,
        .val_handle = &hr_chr_handle,
        .cpfd = 0,
      },
      {0}
    }
  },
  {
    .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = &ml_buffer_service.svc_uuid.u,
    .includes = NULL,
    .characteristics = (struct ble_gatt_chr_def[]) {
      {
        .uuid = &ml_results_chr_uuid.u,
        .access_cb = ml_results_chr_access_cb,
        .arg = NULL,
        .descriptors = NULL,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .min_key_size = 0,
        .val_handle = &ml_results_chr_handle,
        .cpfd = 0,
      },
      {
        .uuid = &ml_errors_chr_uuid.u,
        .access_cb = ml_errors_chr_access_cb,
        .arg = NULL,
        .descriptors = NULL,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .min_key_size = 0,
        .val_handle = &ml_errors_chr_handle,
        .cpfd = 0,
      },
      BLE_GATT_BUFFER_CHRS_DEF(ml_buffer_service),
      {0},
    }
  },
  {0},
};
// clang-format on

void ble_gatt_att_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg) {
  (void)arg;

  if (ctxt->op == BLE_GATT_REGISTER_OP_DSC) {
    ble_buffer_defs_register(
      ctxt->dsc.svc_def->uuid, ctxt->dsc.chr_def->uuid,
      ctxt->dsc.dsc_def->uuid, ctxt->dsc.handle
    );
  }
}

int ble_gatt_init(void) {
  ble_svc_gatt_init();

  return ble_gatts_count_cfg(gatt_svcs) || ble_gatts_add_svcs(gatt_svcs);
}

void ble_gatt_hr_chr_update(void) {
  ble_gatts_chr_updated(hr_chr_handle);
}
