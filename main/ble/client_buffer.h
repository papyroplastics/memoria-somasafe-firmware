#ifndef BLE_CLIENT_BUFFER_H
#define BLE_CLIENT_BUFFER_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#include <host/ble_att.h>
#include <host/ble_gatt.h>
#include <host/ble_uuid.h>

#include "ble/host.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const ble_uuid128_t ble_buffer_chr_uuid;
extern const ble_uuid128_t ble_buffer_state_chr_uuid;
extern const ble_uuid128_t ble_buffer_size_dsc_uuid;
extern const ble_uuid128_t ble_buffer_pos_dsc_uuid;

#define BLE_BUFFER_STATE_NOT_READY 0
#define BLE_BUFFER_STATE_READY 1
enum buffer_access_state {
  BUF_ACC_NOT_READY = 0,
  BUF_ACC_READY = 1
};

struct ble_client_buffer {
  char* name;

  uint8_t *data;
  uint32_t pos;
  uint32_t size;
  enum buffer_access_state state;
  bool dirty;

  pthread_mutex_t mutex;
  pthread_cond_t cond;

  uint16_t chr_handle;
  uint16_t state_chr_handle;
};

#define BLE_CLIENT_BUFFER_INIT(module) \
  { \
    .name = (module), \
    \
    .data = NULL, \
    .pos = 0, \
    .size = 0, \
    .state = BUF_ACC_NOT_READY, \
    .dirty = true, \
    \
    .mutex = PTHREAD_MUTEX_INITIALIZER, \
    .cond = PTHREAD_COND_INITIALIZER, \
    \
    .chr_handle = 0, \
    .state_chr_handle = 0 \
  }

int ble_client_buffer_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg);
int ble_client_buffer_size_dsc_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg);
int ble_client_buffer_pos_dsc_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg);
int ble_client_buffer_state_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg);

typedef void (*ble_client_buffer_use_cb)(void *arg, bool dirty);

void ble_client_buffer_use(struct ble_client_buffer *buffer,
    ble_client_buffer_use_cb cb, void *arg);
bool ble_client_buffer_try_use(struct ble_client_buffer *buffer,
    ble_client_buffer_use_cb cb, void *arg);
bool ble_client_buffer_lock(struct ble_client_buffer *buffer);
bool ble_client_buffer_try_lock(struct ble_client_buffer *buffer, bool *dirty_out);

void ble_client_invalidate(struct ble_client_buffer *buffer);
void ble_client_buffer_reset(struct ble_client_buffer *buffer);
void ble_client_buffer_unlock(struct ble_client_buffer *buffer);

#define BLE_GATT_BUFFER_CHRS_DEF(buffer) \
  { \
    .uuid = &ble_buffer_chr_uuid.u, \
    .access_cb = ble_client_buffer_chr_access_cb, \
    .arg = &(buffer), \
    .descriptors = (struct ble_gatt_dsc_def[]) { \
      { \
        .uuid = &ble_buffer_size_dsc_uuid.u, \
        .att_flags = GATT_DSC_READ_FLAGS | GATT_DSC_WRITE_FLAGS, \
        .min_key_size = 0, \
        .access_cb = ble_client_buffer_size_dsc_access_cb, \
        .arg = &(buffer), \
      }, \
      { \
        .uuid = &ble_buffer_pos_dsc_uuid.u, \
        .att_flags = GATT_DSC_READ_FLAGS | GATT_DSC_WRITE_FLAGS, \
        .min_key_size = 0, \
        .access_cb = ble_client_buffer_pos_dsc_access_cb, \
        .arg = &(buffer), \
      }, \
      {0}, \
    }, \
    .flags = GATT_CHR_READ_FLAGS | GATT_CHR_WRITE_FLAGS, \
    .min_key_size = 0, \
    .val_handle = &(buffer).chr_handle, \
    .cpfd = NULL, \
  }, \
  { \
    .uuid = &ble_buffer_state_chr_uuid.u, \
    .access_cb = ble_client_buffer_state_chr_access_cb, \
    .arg = &(buffer), \
    .descriptors = NULL, \
    .flags = GATT_CHR_READ_FLAGS | GATT_CHR_WRITE_FLAGS | GATT_CHR_NOTIFY_FLAGS, \
    .min_key_size = 0, \
    .val_handle = &(buffer).state_chr_handle, \
    .cpfd = NULL, \
  }

#ifdef __cplusplus
}  // extern "C"
#endif

#endif // BLE_CLIENT_BUFFER_H
