#include <stdlib.h>
#include <stdint.h>

#include <esp_log.h>
#include <host/ble_att.h>
#include <host/ble_gatt.h>

#include "common.h"
#include "ble/client_buffer.h"
#include "ble/buffer_defs.h"
#include "utils/worker.h"

static const char tag[] = APP_TAG "-client-buffer";

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
  if (OS_MBUF_PKTLEN(om) != sizeof(*value_out)) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }

  if (os_mbuf_copydata(om, 0, sizeof(*value_out), value_out) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  return 0;
}

static int read_u8(struct os_mbuf *om, uint8_t *value_out) {
  if (OS_MBUF_PKTLEN(om) != sizeof(*value_out)) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }

  if (os_mbuf_copydata(om, 0, sizeof(*value_out), value_out) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  return 0;
}

static void buffer_state_reset_work(void *arg) {
  struct ble_gatt_buffer_service *service = arg;
  struct ble_client_buffer *buffer = &service->buffer;

  pthread_mutex_lock(&buffer->mutex);
  buffer->ready = false;
  pthread_mutex_unlock(&buffer->mutex);

  if (service->state_chr_handle != 0) {
    ble_gatts_chr_updated(service->state_chr_handle);
  }
}

static int ble_client_buffer_set_ready(struct ble_client_buffer *buffer) {
  pthread_mutex_lock(&buffer->mutex);
  buffer->ready = true;
  pthread_cond_broadcast(&buffer->cond);
  pthread_mutex_unlock(&buffer->mutex);
  return 0;
}

static int enqueue_buffer_state_reset(struct ble_gatt_buffer_service *service) {
  if (worker_queue_push_task(buffer_state_reset_work, service) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  return 0;
}

bool ble_client_buffer_lock(struct ble_client_buffer *buffer) {
  pthread_mutex_lock(&buffer->mutex);
  while (!buffer->ready) {
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

  if (!buffer->ready) {
    pthread_mutex_unlock(&buffer->mutex);
    return false;
  }

  bool dirty = buffer->dirty;
  buffer->dirty = false;
  if (dirty_out != NULL) {
    *dirty_out = dirty;
  }
  return true;
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

  uint16_t value_len = OS_MBUF_PKTLEN(om);
  if (value_len == 0) {
    return 0;
  }

  if (pthread_mutex_trylock(&buffer->mutex) != 0) {
    return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
  }

  int err = 0;
  if (buffer->ready) {
    err = BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    goto out;
  }

  if (buffer->data == NULL || buffer->pos + value_len > buffer->size) {
    ESP_LOGE(tag, "invalid write, pos: %d, len: %d, size: %d", 
        buffer->pos, value_len, buffer->size);
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

  if (buffer->ready) {
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
    ESP_LOGE(tag, "failed to allocate %d bytes for client buffer", size);
    err = BLE_ATT_ERR_INSUFFICIENT_RES;
    goto out;
  }

  buffer->size = size;

out:
  pthread_mutex_unlock(&buffer->mutex);
  return err;
}

static int ble_client_buffer_set_pos(struct ble_client_buffer *buffer, uint32_t pos) {
  if (pthread_mutex_trylock(&buffer->mutex) != 0) {
    return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
  }

  if (buffer->ready) {
    pthread_mutex_unlock(&buffer->mutex);
    return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
  }

  if (pos > buffer->size) {
    buffer->pos = buffer->size;
  } else {
    buffer->pos = pos;
  }

  pthread_mutex_unlock(&buffer->mutex);
  return 0;
}

int ble_client_buffer_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)arg;

  struct ble_gatt_buffer_service *service = arg;

  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
      return ble_client_buffer_write(&service->buffer, ctxt->om);

    default:
      ESP_LOGE(tag, "illegal operation to buffer chr with code: %d", ctxt->op);
      return BLE_ATT_ERR_UNLIKELY;
  }
}

int ble_client_buffer_size_dsc_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)arg;

  struct ble_gatt_buffer_service *service = arg;

  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_DSC:
      return append_u32(ctxt->om, service->buffer.size);
    case BLE_GATT_ACCESS_OP_WRITE_DSC: {
      uint32_t size = 0;
      int err = read_u32(ctxt->om, &size);
      if (err != 0) {
        return err;
      }
      return ble_client_buffer_set_size(&service->buffer, size);
    }
    default:
      ESP_LOGE(tag, "illegal operation to buffer size dsc with code: %d", ctxt->op);
      return BLE_ATT_ERR_UNLIKELY;
  }
}

int ble_client_buffer_pos_dsc_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)arg;

  struct ble_gatt_buffer_service *service = arg;

  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_DSC:
      return append_u32(ctxt->om, service->buffer.pos);
    case BLE_GATT_ACCESS_OP_WRITE_DSC: {
      uint32_t pos = 0;
      int err = read_u32(ctxt->om, &pos);
      if (err != 0) {
        return err;
      }
      return ble_client_buffer_set_pos(&service->buffer, pos);
    }
    default:
      ESP_LOGE(tag, "illegal operation to buffer pos dsc with code: %d", ctxt->op);
      return BLE_ATT_ERR_UNLIKELY;
  }
}

int ble_client_buffer_state_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)attr_handle;

  struct ble_gatt_buffer_service *service = arg;

  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
      if (pthread_mutex_trylock(&service->buffer.mutex) == 0) {
        uint8_t state = service->buffer.ready ? BLE_BUFFER_STATE_READY : BLE_BUFFER_STATE_NOT_READY;
        pthread_mutex_unlock(&service->buffer.mutex);
        return append_u8(ctxt->om, state);
      }
      return append_u8(ctxt->om, BLE_BUFFER_STATE_READY);
    case BLE_GATT_ACCESS_OP_WRITE_CHR: {
      uint8_t state = 0;
      int err = read_u8(ctxt->om, &state);
      if (err != 0) {
        return err;
      }
      if (state == BLE_BUFFER_STATE_READY) {
        return ble_client_buffer_set_ready(&service->buffer);
      }
      if (state == BLE_BUFFER_STATE_NOT_READY) {
        return enqueue_buffer_state_reset(service);
      }
      return BLE_ATT_ERR_UNLIKELY;
    }
    default:
      ESP_LOGE(tag, "illegal operation to buffer state chr with code: %d", ctxt->op);
      return BLE_ATT_ERR_UNLIKELY;
  }
}
