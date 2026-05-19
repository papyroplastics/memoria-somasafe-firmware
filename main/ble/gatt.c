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
#include "ble/client_buffer.h"
#include "ppg/sensor.h"

static const char tag[] = APP_TAG "-gatt";

static uint16_t hr_chr_handle;
const ble_uuid16_t hr_svc_uuid = BLE_UUID16_INIT(0x180D);
static const ble_uuid16_t hr_chr_uuid = BLE_UUID16_INIT(0x2A37);

struct ble_gatt_buffer_service model_buffer_service = {
  .buffer = BLE_CLIENT_BUFFER_INIT,
  .svc_uuid = BLE_UUID128_INIT(
      0x38, 0x27, 0x43, 0xd4, 0xda, 0xb7, 0x43, 0xfe,
      0x92, 0x24, 0x43, 0x75, 0x40, 0x38, 0x52, 0xa4,
  ),
  .chr_uuid = BLE_UUID128_INIT(
      0x5a, 0xf2, 0x87, 0x8c, 0xa3, 0x6f, 0x4d, 0xc0,
      0x86, 0x8a, 0xcb, 0x1d, 0xa6, 0xf3, 0x04, 0x8f,
  ),
  .size_dsc_uuid = BLE_UUID128_INIT(
      0x85, 0x25, 0x7e, 0x0a, 0x4b, 0xae, 0x48, 0xab,
      0x87, 0x55, 0x34, 0xbf, 0xc8, 0x84, 0x73, 0x23,
  ),
  .pos_dsc_uuid = BLE_UUID128_INIT(
      0xae, 0x4b, 0x37, 0x79, 0x20, 0x0f, 0x4a, 0x48,
      0xaf, 0x57, 0xbe, 0xb6, 0x9c, 0x5c, 0x56, 0xf5,
  ),
  .sha_dsc_uuid = BLE_UUID128_INIT(
      0xa0, 0x54, 0xbc, 0x59, 0x48, 0xc6, 0x45, 0x95,
      0xa0, 0x8f, 0xea, 0xf2, 0x8d, 0x87, 0x7e, 0x71,
  ),
};

struct ble_gatt_buffer_service *gatt_buffer_services[] = {
  &model_buffer_service,
};
const size_t gatt_buffer_services_len =
  sizeof(gatt_buffer_services) / sizeof(gatt_buffer_services[0]);

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

// clang-format off
static struct ble_gatt_svc_def gatt_svcs[] = {
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
  BLE_GATT_BUFFER_SERVICE_DEF(model_buffer_service),
  {0},
};
// clang-format on

void gatt_att_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg) {
  (void)arg;

  if (ctxt->op == BLE_GATT_REGISTER_OP_DSC) {
    const ble_uuid_t *svc_uuid = ctxt->dsc.svc_def->uuid;
    const ble_uuid_t *chr_uuid = ctxt->dsc.chr_def->uuid;
    const ble_uuid_t *dsc_uuid = ctxt->dsc.dsc_def->uuid;

    for (size_t i = 0; i < gatt_buffer_services_len; i++) {
      struct ble_gatt_buffer_service *service = gatt_buffer_services[i];

      if (ble_uuid_cmp(svc_uuid, &service->svc_uuid.u) == 0 &&
          ble_uuid_cmp(chr_uuid, &service->chr_uuid.u) == 0) {

        if (ble_uuid_cmp(dsc_uuid, &service->size_dsc_uuid.u) == 0) {
          service->size_dsc_handle = ctxt->dsc.handle;
          return;
        }

        if (ble_uuid_cmp(dsc_uuid, &service->pos_dsc_uuid.u) == 0) {
          service->pos_dsc_handle = ctxt->dsc.handle;
          return;
        }

        if (ble_uuid_cmp(dsc_uuid, &service->sha_dsc_uuid.u) == 0) {
          service->sha_dsc_handle = ctxt->dsc.handle;
          return;
        }

      }
    }
  }
}

int gatt_init(void) {
  ble_svc_gatt_init();

  return ble_gatts_count_cfg(gatt_svcs) || ble_gatts_add_svcs(gatt_svcs);
}

void gatt_hr_chr_update(void) {
  ble_gatts_chr_updated(hr_chr_handle);
}
