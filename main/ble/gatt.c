#include <stdint.h>
#include <stddef.h>
#include <esp_log.h>
#include <host/ble_hs.h>
#include <host/ble_gatt.h>
#include <host/ble_att.h>
#include <host/ble_uuid.h>
#include <services/gatt/ble_svc_gatt.h>

#include "common.h"
#include "ble/gap.h"
#include "ble/gatt.h"
#include "ble/client_buffer.h"
#include "ppg/service.h"
#include "ml/service.h"

static const char tag[] = APP_TAG "-gatt";

int noop_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)attr_handle;

  char *chr_name = arg;
  ESP_LOGE(tag, "illegal acces on inoperable %s chr with code: %d", chr_name, ctxt->op);

  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
    case BLE_GATT_ACCESS_OP_READ_DSC:
      return BLE_ATT_ERR_READ_NOT_PERMITTED;
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
    case BLE_GATT_ACCESS_OP_WRITE_DSC:
      return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
  }

  return BLE_ATT_ERR_UNLIKELY;
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
    .uuid = &ml_svc_uuid.u,
    .includes = NULL,
    .characteristics = (struct ble_gatt_chr_def[]) {
      {
        .uuid = &ml_results_chr_uuid.u,
        .access_cb = noop_chr_access_cb,
        .arg = "ml result",
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
      BLE_GATT_BUFFER_CHRS_DEF(ml_model_buffer),
      {0},
    }
  },
  {0},
};
// clang-format on

int ble_gatt_task_prepare(void) {
  ble_svc_gatt_init();

  return ble_gatts_count_cfg(gatt_svcs) || ble_gatts_add_svcs(gatt_svcs);
}

int ble_gatt_notify_chr(uint16_t chr_handle, const uint8_t *payload, uint16_t payload_len) {
  uint16_t conn = ble_gap_get_conn_handle();
  if (conn == BLE_HS_CONN_HANDLE_NONE) {
    return 1;
  }

  struct os_mbuf *om = ble_hs_mbuf_from_flat(payload, payload_len);
  if (om == NULL) {
    ESP_LOGE(tag, "failed to allocate notify buffer");
    return 1;
  }

  int err = ble_gatts_notify_custom(conn, chr_handle, om);
  if (err != 0) {
    ESP_LOGE(tag, "notify failed with err %d", err);
  }

  return 0;
}

