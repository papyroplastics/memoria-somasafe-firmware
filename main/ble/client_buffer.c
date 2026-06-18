#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>

#include <esp_log.h>
#include <host/ble_att.h>
#include <host/ble_gatt.h>

#include "common.h"
#include "ble/client_buffer.h"
#include "utils/worker.h"

static const char tag[] = APP_TAG "-client-buffer";

const ble_uuid128_t ble_buffer_chr_uuid = BLE_UUID128_INIT(
    0x5a, 0xf2, 0x87, 0x8c, 0xa3, 0x6f, 0x4d, 0xc0,
    0x86, 0x8a, 0xcb, 0x1d, 0xa6, 0xf3, 0x04, 0x8f,
);
const ble_uuid128_t ble_buffer_state_chr_uuid = BLE_UUID128_INIT(
    0x19, 0x7d, 0x1c, 0x5b, 0x02, 0xd1, 0x4c, 0xb9,
    0x9b, 0x22, 0x90, 0x8c, 0x0a, 0x33, 0x4d, 0x79,
);
const ble_uuid128_t ble_buffer_size_dsc_uuid = BLE_UUID128_INIT(
    0x85, 0x25, 0x7e, 0x0a, 0x4b, 0xae, 0x48, 0xab,
    0x87, 0x55, 0x34, 0xbf, 0xc8, 0x84, 0x73, 0x23,
);
const ble_uuid128_t ble_buffer_pos_dsc_uuid = BLE_UUID128_INIT(
    0xae, 0x4b, 0x37, 0x79, 0x20, 0x0f, 0x4a, 0x48,
    0xaf, 0x57, 0xbe, 0xb6, 0x9c, 0x5c, 0x56, 0xf5,
);

static int append_u32(struct os_mbuf *om, uint32_t value) {
  if (os_mbuf_append(om, &value, sizeof(value)) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  return 0;
}

static int append_u8(struct os_mbuf *om, uint8_t value) {
  if (os_mbuf_append(om, &value, sizeof(value)) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  return 0;
}

static int read_u32(struct os_mbuf *om, uint32_t *value_out) {
  if (os_mbuf_copydata(om, 0, sizeof(*value_out), value_out) != 0) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;;
  }

  return 0;
}

static int read_u8(struct os_mbuf *om, uint8_t *value_out) {
  if (os_mbuf_copydata(om, 0, sizeof(*value_out), value_out) != 0) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }

  return 0;
}

static int ble_client_buffer_read(struct ble_client_buffer *buffer,
    uint16_t conn_handle, struct os_mbuf *om) {
  if (buffer->data == NULL && buffer->size != 0) {
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
  }

  uint32_t remaining = buffer->size - buffer->pos;
  if (remaining == 0) {
    return 0;
  }

  uint16_t mtu = ble_att_mtu(conn_handle);
  if (mtu < BLE_ATT_MTU_DFLT) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  // MTU-2 bytes are sent at most because some clients interpret 
  // MTU-1 bytes as a long characteristic and send ATT_READ_BLOB_REQ
  // which should be fine in theory but for some reason ATT_READ_BLOB_RSP
  // arrive with no data to the client so it's better not to use them
  uint32_t read_len = remaining > mtu - 2 ? mtu - 2 : remaining;

  if (os_mbuf_append(om, buffer->data + buffer->pos, read_len) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  buffer->pos += read_len;

  return 0;
}

static void buffer_state_reset_work(void *arg) {
  struct ble_client_buffer *buffer = arg;

  pthread_mutex_lock(&buffer->mutex);
  buffer->state = BUF_ACC_NOT_READY;
  pthread_mutex_unlock(&buffer->mutex);

  ble_gatts_chr_updated(buffer->state_chr_handle);
}

bool ble_client_buffer_lock(struct ble_client_buffer *buffer) {
  pthread_mutex_lock(&buffer->mutex);
  while (buffer->state == BUF_ACC_NOT_READY) {
    pthread_cond_wait(&buffer->cond, &buffer->mutex);
  }

  bool dirty = buffer->dirty;
  buffer->dirty = false;
  return dirty;
}

bool ble_client_buffer_try_lock(struct ble_client_buffer *buffer, bool *dirty_out) {
  if (pthread_mutex_trylock(&buffer->mutex) != 0) {
    return false;
  }

  if (buffer->state == BUF_ACC_NOT_READY) {
    pthread_mutex_unlock(&buffer->mutex);
    return false;
  }

  ESP_LOGI(tag, "try-locked client buffer \"%s\" with dirty=%d", buffer->name, buffer->dirty);

  if (dirty_out != NULL) {
    *dirty_out = buffer->dirty;
  }
  buffer->dirty = false;
  return true;
}

void ble_client_invalidate(struct ble_client_buffer *buffer) {
  ESP_LOGI(tag, "invalidating client buffer \"%s\"", buffer->name);
  buffer->state = BUF_ACC_NOT_READY;
  pthread_mutex_unlock(&buffer->mutex);
  ble_gatts_chr_updated(buffer->state_chr_handle);
}

void ble_client_buffer_unlock(struct ble_client_buffer *buffer) {
  pthread_mutex_unlock(&buffer->mutex);
}

void ble_client_buffer_use(struct ble_client_buffer *buffer,
    ble_client_buffer_use_cb cb, void *arg) {
  bool dirty = ble_client_buffer_lock(buffer);
  if (cb != NULL) {
    cb(arg, dirty);
  }
  ble_client_buffer_unlock(buffer);
}

bool ble_client_buffer_try_use(struct ble_client_buffer *buffer,
    ble_client_buffer_use_cb cb, void *arg) {
  bool dirty = false;
  if (!ble_client_buffer_try_lock(buffer, &dirty)) {
    return false;
  }

  if (cb != NULL) {
    cb(arg, dirty);
  }

  ble_client_buffer_unlock(buffer);
  return true;
}

static int ble_client_buffer_write(struct ble_client_buffer *buffer, struct os_mbuf *om) {
  if (pthread_mutex_trylock(&buffer->mutex) != 0) {
    return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
  }

  int err = 0;
  if (buffer->state == BUF_ACC_READY) {
    err = BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    goto out;
  }

  uint16_t value_len = OS_MBUF_PKTLEN(om);
  if (value_len == 0) {
    return 0;
  }

  if (buffer->data == NULL || buffer->pos + value_len > buffer->size) {
    ESP_LOGE(tag, "invalid write on buffer \"%s\", pos: %d, len: %d, size: %d", 
        buffer->name, buffer->pos, value_len, buffer->size);
    err = BLE_ATT_ERR_INSUFFICIENT_RES;
    goto out;
  }

  if (os_mbuf_copydata(om, 0, value_len, buffer->data + buffer->pos) != 0) {
    err = BLE_ATT_ERR_UNLIKELY;
    goto out;
  }

  buffer->pos += value_len;
  buffer->dirty = true;

out:
  pthread_mutex_unlock(&buffer->mutex);
  return err;
}

static int ble_client_buffer_set_size(struct ble_client_buffer *buffer, uint32_t size) {
  int err = 0;

  if (pthread_mutex_trylock(&buffer->mutex) != 0) {
    return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
  }

  if (buffer->state == BUF_ACC_READY) {
    err = BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    goto out;
  }

  buffer->pos = 0;

  if (size == buffer->size) {
    goto out;
  }

  if (buffer->data != NULL) {
    free(buffer->data);
    buffer->data = NULL;
  }

  buffer->dirty = true;

  if (size == 0) {
    buffer->size = 0;
    goto out;
  }

  buffer->data = malloc(size);

  if (buffer->data == NULL) {
    buffer->size = 0;
    ESP_LOGE(tag, "failed to allocate %d bytes for client buffer \"%s\"", buffer->name, size);
    err = BLE_ATT_ERR_INSUFFICIENT_RES;
    goto out;
  }

  ESP_LOGI(tag, "set buffer size to %d on buffer \"%s\"", size, buffer->name);
  buffer->size = size;

out:
  pthread_mutex_unlock(&buffer->mutex);
  return err;
}

static int ble_client_buffer_set_pos(struct ble_client_buffer *buffer, uint32_t pos) {
  if (pthread_mutex_trylock(&buffer->mutex) != 0) {
    return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
  }

  if (buffer->state == BUF_ACC_READY) {
    pthread_mutex_unlock(&buffer->mutex);
    return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
  }

  if (pos > buffer->size) {
    buffer->pos = buffer->size;
  } else {
    buffer->pos = pos;
  }

  pthread_mutex_unlock(&buffer->mutex);
  ESP_LOGI(tag, "set buffer position to %d on byffer \"%s\" ", buffer->pos, buffer->name);
  return 0;
}

int ble_client_buffer_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)arg;

  struct ble_client_buffer *buffer = arg;

  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
      return ble_client_buffer_read(buffer, conn_handle, ctxt->om);
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
      return ble_client_buffer_write(buffer, ctxt->om);

    default:
      ESP_LOGE(tag, "illegal operation on buffer \"%s\" with code: %d", buffer->name, ctxt->op);
      return BLE_ATT_ERR_UNLIKELY;
  }
}

int ble_client_buffer_size_dsc_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)arg;

  struct ble_client_buffer *buffer = arg;

  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_DSC:
      return append_u32(ctxt->om, buffer->size);

    case BLE_GATT_ACCESS_OP_WRITE_DSC: {
      uint32_t size = 0;
      int err = read_u32(ctxt->om, &size);
      if (err != 0) {
        return err;
      }
      return ble_client_buffer_set_size(buffer, size);
    }
    default:
      ESP_LOGE(tag, "illegal operation to buffer \"%s\" size dsc with code: %d", 
          buffer->name, ctxt->op);
      return BLE_ATT_ERR_UNLIKELY;
  }
}

int ble_client_buffer_pos_dsc_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)arg;

  struct ble_client_buffer *buffer = arg;

  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_DSC:
      return append_u32(ctxt->om, buffer->pos);
    case BLE_GATT_ACCESS_OP_WRITE_DSC: {
      uint32_t pos = 0;
      int err = read_u32(ctxt->om, &pos);
      if (err != 0) {
        return err;
      }
      return ble_client_buffer_set_pos(buffer, pos);
    }
    default:
      ESP_LOGE(tag, "illegal operation to buffer \"%s\" pos dsc with code: %d",
          buffer->name, ctxt->op);
      return BLE_ATT_ERR_UNLIKELY;
  }
}

int ble_client_buffer_state_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)attr_handle;

  struct ble_client_buffer *buffer = arg;

  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
      if (pthread_mutex_trylock(&buffer->mutex) == 0) {
        int err = append_u8(ctxt->om, buffer->state);
        pthread_mutex_unlock(&buffer->mutex);
        return err;
      }
      return append_u8(ctxt->om, BUF_ACC_READY);

    case BLE_GATT_ACCESS_OP_WRITE_CHR: {
      uint8_t state = 0;
      int err = read_u8(ctxt->om, &state);
      if (err != 0) {
        return err;
      }

      switch (state) {
        case BUF_ACC_NOT_READY:
          ESP_LOGI(tag, "setting buffer \"%s\" state to NOT_READY", buffer->name);
          if (worker_queue_push_task(buffer_state_reset_work, buffer) != 0) {
            return BLE_ATT_ERR_UNLIKELY;
          }
          return 0;

        case BUF_ACC_READY:
          if (pthread_mutex_trylock(&buffer->mutex) != 0) {
            ESP_LOGE(tag, "client re-wrote ready state on buffer \"%s\"", buffer->name);
            return BLE_ATT_ERR_VALUE_NOT_ALLOWED;

          } else if (buffer->data == NULL) {
            ESP_LOGE(tag, "client tried to ready empty buffer \"%s\"", buffer->name);
            return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
          }

          ESP_LOGI(tag, "setting buffer \"%s\" state to READY", buffer->name);
          buffer->state = BUF_ACC_READY;
          pthread_cond_signal(&buffer->cond);
          pthread_mutex_unlock(&buffer->mutex);
          return 0;

        default:
          ESP_LOGE(tag, "client wrote illegal state %d on buffer \"%s\"", state, buffer->name);
          return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
      }
    }
    default:
      ESP_LOGE(tag, "illegal operation on buffer \"%s\" state chr with code: %d", 
          buffer->name, ctxt->op);
      return BLE_ATT_ERR_UNLIKELY;
  }
}
