
#include "ble/client_buffer.h"
#include "ble/buffer_defs.h"
#include <stdbool.h>

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


bool register_client_buffer(
    const ble_uuid_t *svc_uuid, const ble_uuid_t *chr_uuid,
    const ble_uuid_t *dsc_uuid, uint16_t dsc_handle) {

  for (size_t i = 0; i < gatt_buffer_services_len; i++) {
    struct ble_gatt_buffer_service *service = gatt_buffer_services[i];

    if (ble_uuid_cmp(svc_uuid, &service->svc_uuid.u) == 0 &&
        ble_uuid_cmp(chr_uuid, &service->chr_uuid.u) == 0) {

      if (ble_uuid_cmp(dsc_uuid, &service->size_dsc_uuid.u) == 0) {
        service->size_dsc_handle = dsc_handle;
        return true;
      }

      if (ble_uuid_cmp(dsc_uuid, &service->pos_dsc_uuid.u) == 0) {
        service->pos_dsc_handle = dsc_handle;
        return true;
      }

      if (ble_uuid_cmp(dsc_uuid, &service->sha_dsc_uuid.u) == 0) {
        service->sha_dsc_handle = dsc_handle;
        return true;
      }
    }
  }

  return false;
}

