#include <stdint.h>
#include <esp_log.h>
#include <host/ble_hs.h>
#include <host/ble_gatt.h>
#include <host/ble_att.h>
#include <host/ble_uuid.h>
#include <lwip/def.h>
#include <services/gatt/ble_svc_gatt.h>

#include "common.h"
#include "ble/gatt.h"
#include "ble/host.h"
#include "ml/model.h"
#include "ppg/sensor.h"

static const char tag[] = APP_TAG "-gatt";

static uint16_t hr_chr_handle;
static uint16_t model_chr_handle;

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

        ESP_LOGI(tag, "%s %d read value %d from HR characteristic",
            conn_handle == BLE_HS_CONN_HANDLE_NONE ? "nimble stack" : "client",
            conn_handle == BLE_HS_CONN_HANDLE_NONE ? conn_handle : 0, hr_chr_value[1]);

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

static int model_err_to_att_err(enum model_load_err err) {
  switch (err) {
    case NONE:
      return 0;
    case INSUFICCIENT_SPACE:
      return BLE_ATT_ERR_INSUFFICIENT_RES;
    case SHA_HW_FAILURE:
      return BLE_ATT_ERR_UNLIKELY;
  }

  return BLE_ATT_ERR_UNLIKELY;
}

static int append_u32(struct os_mbuf *om, uint32_t value) {
  uint32_t net_value = htonl(value);
  if (os_mbuf_append(om, &net_value, sizeof(net_value)) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  return 0;
}

static int read_u32(struct os_mbuf *om, uint32_t *value_out) {
  uint32_t net_value = 0;

  if (OS_MBUF_PKTLEN(om) != sizeof(net_value)) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }

  if (os_mbuf_copydata(om, 0, sizeof(net_value), &net_value) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  *value_out = ntohl(net_value);
  return 0;
}

static int model_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)attr_handle;
  (void)arg;

  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_WRITE_CHR: {
      uint16_t value_len = OS_MBUF_PKTLEN(ctxt->om);
      if (value_len == 0) {
        return 0;
      }

      uint8_t value_buf[value_len];
      if (os_mbuf_copydata(ctxt->om, 0, value_len, value_buf) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
      }

      return model_err_to_att_err(model_write(value_buf, value_len));
    }

    default:
      ESP_LOGE(tag, "illegal operation to model chr with code: %d", ctxt->op);
      return BLE_ATT_ERR_UNLIKELY;
  }
}

static int model_size_dsc_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)attr_handle;
  (void)arg;

  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_DSC: {
      size_t size = model_get_size();
      if (size > UINT32_MAX) {
        return BLE_ATT_ERR_UNLIKELY;
      }

      return append_u32(ctxt->om, (uint32_t)size);
    }

    case BLE_GATT_ACCESS_OP_WRITE_DSC: {
      uint32_t size_u32 = 0;
      int err = read_u32(ctxt->om, &size_u32);
      if (err != 0) {
        return err;
      }

      return model_err_to_att_err(model_set_size(size_u32));
    }

    default:
      ESP_LOGE(tag, "illegal operation to model size dsc with code: %d", ctxt->op);
      return BLE_ATT_ERR_UNLIKELY;
  }
}

static int model_pos_dsc_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)attr_handle;
  (void)arg;

  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_DSC: {
      size_t pos = model_get_pos();
      if (pos > UINT32_MAX) {
        return BLE_ATT_ERR_UNLIKELY;
      }

      return append_u32(ctxt->om, (uint32_t)pos);
    }

    case BLE_GATT_ACCESS_OP_WRITE_DSC: {
      uint32_t pos_u32 = 0;
      int err = read_u32(ctxt->om, &pos_u32);
      if (err != 0) {
        return err;
      }

      return model_err_to_att_err(model_set_pos(pos_u32));
    }

    default:
      ESP_LOGE(tag, "illegal operation to model pos dsc with code: %d", ctxt->op);
      return BLE_ATT_ERR_UNLIKELY;
  }
}

static int model_sha_dsc_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)attr_handle;
  (void)arg;

  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_DSC: {
      uint8_t checksum[32];
      int err = model_err_to_att_err(model_get_checksum(checksum));
      if (err != 0) {
        return err;
      }

      if (os_mbuf_append(ctxt->om, checksum, sizeof(checksum)) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
      }

      return 0;
    }

    default:
      ESP_LOGE(tag, "illegal operation to model sha dsc with code: %d", ctxt->op);
      return BLE_ATT_ERR_UNLIKELY;
  }
}

// clang-format off
static const struct ble_gatt_svc_def gatt_svcs[] = {
  {
    .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = &hr_svc_uuid.u,
    .characteristics = (struct ble_gatt_chr_def[]) {
      {
        .uuid = &hr_chr_uuid.u,
        .access_cb = hr_chr_access_cb,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &hr_chr_handle
      },
      {0}
    }
  },

  {
    .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = &model_svc_uuid.u,
    .characteristics = (struct ble_gatt_chr_def[]) {
      {
        .uuid = &model_chr_uuid.u,
        .access_cb = model_chr_access_cb,
        .descriptors = (struct ble_gatt_dsc_def[]) {
          {
            .uuid = &model_size_dsc_uuid.u,
            .att_flags = BLE_ATT_F_READ | BLE_ATT_F_WRITE,
            .access_cb = model_size_dsc_access_cb
          },
          {
            .uuid = &model_pos_dsc_uuid.u,
            .att_flags = BLE_ATT_F_READ | BLE_ATT_F_WRITE,
            .access_cb = model_pos_dsc_access_cb
          },
          {
            .uuid = &model_sha_dsc_uuid.u,
            .att_flags = BLE_ATT_F_READ,
            .access_cb = model_sha_dsc_access_cb
          },
          {0}
        },
        .flags = BLE_GATT_CHR_F_WRITE,
        .val_handle = &model_chr_handle
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

void gatt_hr_chr_update(void) {
  ble_gatts_chr_updated(hr_chr_handle);
}
