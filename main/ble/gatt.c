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
#include "ml/service.h"

static const char tag[] = APP_TAG "-gatt";

// PPG service
const ble_uuid128_t ppg_svc_uuid = BLE_UUID128_INIT(
    0x8e, 0x3c, 0x5d, 0x0a, 0x7b, 0x1e, 0x2f, 0x9c,
    0x56, 0x4d, 0x8b, 0x3a, 0x10, 0xf2, 0xe4, 0xc7,
);

uint16_t ppg_data_chr_handle;
uint8_t ppg_data_chr_notify;
const ble_uuid128_t ppg_data_chr_uuid = BLE_UUID128_INIT(
    0x9d, 0x4a, 0x0c, 0x6b, 0x1f, 0x2e, 0xd3, 0xa7,
    0x89, 0x4f, 0x12, 0x5c, 0x47, 0xa3, 0xe9, 0xb8,
);

// ML service
const ble_uuid128_t ml_svc_uuid = BLE_UUID128_INIT(
    0x38, 0x27, 0x43, 0xd4, 0xda, 0xb7, 0x43, 0xfe,
    0x92, 0x24, 0x43, 0x75, 0x40, 0x38, 0x52, 0xa4,
);

uint16_t ml_result_chr_handle;
uint8_t ml_result_chr_notify;
const ble_uuid128_t ml_result_chr_uuid = BLE_UUID128_INIT(
    0x54, 0x3c, 0xc2, 0x5a, 0x71, 0x1f, 0x4d, 0xfa,
    0x9c, 0x4b, 0xc1, 0x4f, 0x86, 0xd0, 0x28, 0x72
);
uint16_t ml_errors_chr_handle;
uint8_t ml_errors_chr_notify;
const ble_uuid128_t ml_errors_chr_uuid = BLE_UUID128_INIT(
    0x0e, 0x33, 0x1f, 0xcf, 0x7a, 0x8a, 0x42, 0xc6,
    0xa5, 0x6e, 0x25, 0x5a, 0x42, 0x8c, 0x8b, 0x9c
);

const ble_uuid128_t svc_uuid128[] = { ppg_svc_uuid, ml_svc_uuid };
const uint8_t svc_uuid128_cnt = sizeof(svc_uuid128) / sizeof(*svc_uuid128);

static int noop_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg);

// clang-format off
static struct ble_gatt_svc_def gatt_svcs[] = {
  {
    .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = &ppg_svc_uuid.u,
    .includes = NULL,
    .characteristics = (struct ble_gatt_chr_def[]) {
      {
        .uuid = &ppg_data_chr_uuid.u,
        .access_cb = noop_chr_access_cb,
        .arg = "ppg data",
        .descriptors = NULL,
        .flags = GATT_CHR_NOTIFY_FLAGS,
        .min_key_size = 0,
        .val_handle = &ppg_data_chr_handle,
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
        .uuid = &ml_result_chr_uuid.u,
        .access_cb = noop_chr_access_cb,
        .arg = "ml result",
        .descriptors = NULL,
        .flags = GATT_CHR_NOTIFY_FLAGS,
        .min_key_size = 0,
        .val_handle = &ml_result_chr_handle,
        .cpfd = 0,
      },
      {
        .uuid = &ml_errors_chr_uuid.u,
        .access_cb = noop_chr_access_cb,
        .arg = NULL,
        .descriptors = NULL,
        .flags = GATT_CHR_NOTIFY_FLAGS,
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

void ble_gatt_subscribe_cb(uint16_t attr_handle, uint8_t notify) {
  if (attr_handle == ppg_data_chr_handle) {
    ppg_data_chr_notify = notify;
  } else if (attr_handle == ml_result_chr_handle) {
    ml_result_chr_notify = notify;
  } else if (attr_handle == ml_errors_chr_handle) {
    ml_errors_chr_notify = notify;
  }
}

void ble_gatt_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg) {
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGI(tag, "registered service %s with handle=%d",
                 ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf), ctxt->svc.handle);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGI(tag, "registered characteristic %s with def_handle=%d val_handle=%d",
                 ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                 ctxt->chr.def_handle, ctxt->chr.val_handle);
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        ESP_LOGI(tag, "registered descriptor %s with handle=%d",
                 ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf), ctxt->dsc.handle);
        break;

    default:
        ESP_LOGI(tag, "registered unknown attribute of type=%d", ctxt->op);
        break;
    }
}

static int noop_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)attr_handle;

  char *chr_name = arg;
  ESP_LOGE(tag, "illegal access on inoperable %s chr with code: %d", chr_name, ctxt->op);

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
