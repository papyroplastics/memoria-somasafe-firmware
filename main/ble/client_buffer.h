#ifndef BLE_CLIENT_BUFFER_H
#define BLE_CLIENT_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <host/ble_att.h>
#include <host/ble_gatt.h>
#include <host/ble_uuid.h>

#define BLE_CLIENT_BUFFER_CHECKSUM_LEN 32

struct ble_client_buffer {
  uint8_t *data;
  uint32_t pos;
  uint32_t size;
  bool checksum_dirty;
  uint8_t checksum[BLE_CLIENT_BUFFER_CHECKSUM_LEN];
};

#define BLE_CLIENT_BUFFER_INIT \
  { .data = NULL, .pos = 0, .size = 0, .checksum_dirty = true }

struct ble_gatt_buffer_service {
  struct ble_client_buffer buffer;
  ble_uuid128_t svc_uuid;
  ble_uuid128_t chr_uuid;
  ble_uuid128_t size_dsc_uuid;
  ble_uuid128_t pos_dsc_uuid;
  ble_uuid128_t sha_dsc_uuid;
  uint16_t chr_handle;
  uint16_t size_dsc_handle;
  uint16_t pos_dsc_handle;
  uint16_t sha_dsc_handle;
};

int ble_client_buffer_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg);
int ble_client_buffer_size_dsc_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg);
int ble_client_buffer_pos_dsc_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg);
int ble_client_buffer_sha_dsc_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg);

extern struct ble_gatt_buffer_service *gatt_buffer_services[];
extern const size_t gatt_buffer_services_len;

#define BLE_GATT_BUFFER_SERVICE_DEF(service) \
  { \
    .type = BLE_GATT_SVC_TYPE_PRIMARY, \
    .uuid = &(service).svc_uuid.u, \
    .characteristics = (struct ble_gatt_chr_def[]) { \
      { \
        .uuid = &(service).chr_uuid.u, \
        .access_cb = ble_client_buffer_chr_access_cb, \
        .descriptors = (struct ble_gatt_dsc_def[]) { \
          { \
            .uuid = &(service).size_dsc_uuid.u, \
            .att_flags = BLE_ATT_F_READ | BLE_ATT_F_WRITE, \
            .access_cb = ble_client_buffer_size_dsc_access_cb \
          }, \
          { \
            .uuid = &(service).pos_dsc_uuid.u, \
            .att_flags = BLE_ATT_F_READ | BLE_ATT_F_WRITE, \
            .access_cb = ble_client_buffer_pos_dsc_access_cb \
          }, \
          { \
            .uuid = &(service).sha_dsc_uuid.u, \
            .att_flags = BLE_ATT_F_READ, \
            .access_cb = ble_client_buffer_sha_dsc_access_cb \
          }, \
          {0} \
        }, \
        .flags = BLE_GATT_CHR_F_WRITE, \
        .val_handle = &(service).chr_handle \
      }, \
      {0} \
    } \
  }

#endif  // BLE_CLIENT_BUFFER_H
