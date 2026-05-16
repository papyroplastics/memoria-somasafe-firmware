#include <esp_log.h>
#include <host/ble_hs.h>
#include <host/ble_gatt.h>
#include <host/ble_hs_mbuf.h>
#include <services/gatt/ble_svc_gatt.h>

#include "ble/gatt.h"
#include "ble/host.h"
#include "host/ble_att.h"
#include "host/ble_uuid.h"
#include "ml/model.h"
#include "ppg/sensor.h"

static const char tag[] = "nimble-example-gatt";

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

static int append_size_t_le(struct os_mbuf *om, size_t value) {
  uint8_t value_buf[sizeof(size_t)] = {0};
  for (size_t i = 0; i < sizeof(size_t); ++i) {
    value_buf[i] = (uint8_t)(value >> (8 * i));
  }

  if (os_mbuf_append(om, value_buf, sizeof(value_buf)) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  return 0;
}

static int read_size_t_le(struct os_mbuf *om, size_t *value_out) {
  uint16_t value_len = OS_MBUF_PKTLEN(om);
  if (value_len == 0 || value_len > sizeof(size_t)) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }

  uint8_t value_buf[sizeof(size_t)] = {0};
  if (ble_hs_mbuf_to_flat(om, value_buf, value_len, NULL) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  size_t value = 0;
  for (size_t i = 0; i < value_len; ++i) {
    value |= ((size_t)value_buf[i]) << (8 * i);
  }

  *value_out = value;
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
      if (ble_hs_mbuf_to_flat(ctxt->om, value_buf, value_len, NULL) != 0) {
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
    case BLE_GATT_ACCESS_OP_READ_DSC:
      return append_size_t_le(ctxt->om, model_get_size());

    case BLE_GATT_ACCESS_OP_WRITE_DSC: {
      size_t size = 0;
      int err = read_size_t_le(ctxt->om, &size);
      if (err != 0) {
        return err;
      }

      return model_err_to_att_err(model_set_size(size));
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
    case BLE_GATT_ACCESS_OP_READ_DSC:
      return append_size_t_le(ctxt->om, model_get_pos());

    case BLE_GATT_ACCESS_OP_WRITE_DSC: {
      size_t pos = 0;
      int err = read_size_t_le(ctxt->om, &pos);
      if (err != 0) {
        return err;
      }

      return model_err_to_att_err(model_set_pos(pos));
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
