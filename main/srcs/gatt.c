
#include "common.h"
#include "gatt.h"
#include "ble.h"
#include "ppg.h"

#include <host/ble_gatt.h>
#include <services/gatt/ble_svc_gatt.h>

static const char tag[] = "nimble-example-gatt";

static uint16_t heart_rate_chr_attr_handle;

static int heart_rate_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg) {

  if (likely(ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)) {
    if (attr_handle != heart_rate_chr_attr_handle) {
      ESP_LOGE(tag, "read to HR chr with invalid handle");
      return BLE_ATT_ERR_INVALID_HANDLE;
    }

    const uint8_t hr_chr_value[2] = {
      0, // flags
      ppg_get_hr()
    };

    ESP_LOGI(tag, "read value %d from HR chr (handle %d)", hr_chr_value[1], attr_handle);

    int err = os_mbuf_append(ctxt->om, hr_chr_value, sizeof(hr_chr_value));
    if (err != 0) {
      ESP_LOGI(tag, "unable to answer read on HR chr due to internal error", attr_handle);
      return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    return 0;

  } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
    ESP_LOGE(tag, "illegal write operation to HR chr");
    return BLE_ATT_ERR_WRITE_NOT_PERMITTED;

  } else {
    ESP_LOGE(tag, "illegal operation to HR chr with code: %d", ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
  }
}

// clang-format off
static const struct ble_gatt_svc_def gatt_svcs[] = {
  {
    .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = &heart_rate_svc_uuid.u,
    .characteristics = (struct ble_gatt_chr_def[]) {
      {
        .uuid = &heart_rate_chr_uuid.u,
        .access_cb = heart_rate_chr_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_INDICATE,
        .val_handle = &heart_rate_chr_attr_handle
      },
      {0}
    }
  },
  {0},
};
// clang-format on

int gatt_init(void) {
  ble_svc_gatt_init();

  return ble_gatts_count_cfg(gatt_svcs) || ble_gatts_add_svcs(gatt_svcs);
}

static uint16_t hr_subs_conn_handle;
static uint16_t hr_subs_attr_handle;

void gatt_update_cccd_status(
    uint16_t conn_handle, uint16_t attr_handle,
    uint8_t notification, uint8_t indication) {

  ESP_LOGI(tag, 
      "client %d updated CCCD status on attribute %d to notify %d indicate %d", 
      conn_handle, attr_handle, notification, indication);

  if (conn_handle == heart_rate_chr_attr_handle) {
    if (indication) {
      hr_subs_conn_handle = conn_handle;
      hr_subs_attr_handle = attr_handle;
    } else {
      hr_subs_conn_handle = 0;
      hr_subs_attr_handle = 0;
    }
  }
}

int gatt_hr_attr_signal() {
  if (hr_subs_conn_handle == 0 || hr_subs_attr_handle == 0) {
    return 1;
  }

  int err = ble_gap_conn_find(hr_subs_conn_handle, NULL);
  if (err != 0) {
    ESP_LOGE(tag, "unable to find client %d for HR update", hr_subs_conn_handle);
    hr_subs_conn_handle = 0;
    hr_subs_attr_handle = 0;
    return 1;
  }

  err = ble_gatts_indicate(hr_subs_conn_handle, hr_subs_attr_handle);
  if (err != 0) {
    ESP_LOGE(tag, "error seding update of attribute %d to client %d", 
        hr_subs_attr_handle, hr_subs_conn_handle);
    return 1;
  }

  return 0;
}

