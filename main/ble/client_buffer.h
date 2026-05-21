#ifndef BLE_CLIENT_BUFFER_H
#define BLE_CLIENT_BUFFER_H

#include <stdbool.h>
#include <stdint.h>

#include <host/ble_att.h>
#include <host/ble_gatt.h>
#include <host/ble_uuid.h>

#define SHA256_DIGEST_LENGTH 32

extern const ble_uuid128_t ble_buffer_chr_uuid;
extern const ble_uuid128_t ble_buffer_size_dsc_uuid;
extern const ble_uuid128_t ble_buffer_pos_dsc_uuid;
extern const ble_uuid128_t ble_buffer_sha_dsc_uuid;

struct ble_client_buffer {
  uint8_t *data;
  uint32_t pos;
  uint32_t size;
  bool checksum_dirty;
  uint8_t checksum[SHA256_DIGEST_LENGTH];
};

#define BLE_CLIENT_BUFFER_INIT \
  { .data = NULL, .pos = 0, .size = 0, .checksum_dirty = true }

struct ble_gatt_buffer_service {
  struct ble_client_buffer buffer;
  ble_uuid128_t svc_uuid;
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

#define BLE_GATT_BUFFER_CHR_DEF(service) \
  { \
    .uuid = &ble_buffer_chr_uuid.u, \
    .access_cb = ble_client_buffer_chr_access_cb, \
    .arg = &(service), \
    .descriptors = (struct ble_gatt_dsc_def[]) { \
      { \
        .uuid = &ble_buffer_size_dsc_uuid.u, \
        .att_flags = BLE_ATT_F_READ | BLE_ATT_F_WRITE, \
        .min_key_size = 0, \
        .access_cb = ble_client_buffer_size_dsc_access_cb, \
        .arg = &(service), \
      }, \
      { \
        .uuid = &ble_buffer_pos_dsc_uuid.u, \
        .att_flags = BLE_ATT_F_READ | BLE_ATT_F_WRITE, \
        .min_key_size = 0, \
        .access_cb = ble_client_buffer_pos_dsc_access_cb, \
        .arg = &(service), \
      }, \
      { \
        .uuid = &ble_buffer_sha_dsc_uuid.u, \
        .att_flags = BLE_ATT_F_READ | BLE_ATT_F_WRITE, \
        .min_key_size = 0, \
        .access_cb = ble_client_buffer_sha_dsc_access_cb, \
        .arg = &(service), \
      }, \
      {0}, \
    }, \
    .flags = BLE_GATT_CHR_F_WRITE, \
    .min_key_size = 0, \
    .val_handle = &(service).chr_handle, \
    .cpfd = NULL, \
  }

#define BLE_GATT_BUFFER_SERVICE_DEF(service) BLE_GATT_BUFFER_CHR_DEF(service)

#endif  // BLE_CLIENT_BUFFER_H
