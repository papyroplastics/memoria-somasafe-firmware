#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <esp_log.h>
#include <hal/sha_types.h>
#include <host/ble_att.h>
#include <host/ble_gatt.h>
#include <mbedtls/sha256.h>

#include "common.h"
#include "ble/client_buffer.h"
#include "ble/buffer_defs.h"

static const char tag[] = APP_TAG "-client-buffer";

static int append_u32(struct os_mbuf *om, uint32_t value) {
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

static int ble_client_buffer_write(struct ble_client_buffer *buffer, struct os_mbuf *om) {

  uint16_t value_len = OS_MBUF_PKTLEN(om);
  if (value_len == 0) {
    return 0;
  }

  if (buffer->data == NULL || buffer->pos + value_len > buffer->size) {
    ESP_LOGE(tag, "invalid write, pos: %d, len: %d, size: %d", 
        buffer->pos, value_len, buffer->size);
    return BLE_ATT_ERR_INSUFFICIENT_RES;
  }

  if (os_mbuf_copydata(om, 0, value_len, buffer->data + buffer->pos) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  buffer->pos += value_len;
  buffer->checksum_dirty = true;
  return 0;
}

static int ble_client_buffer_set_size(struct ble_client_buffer *buffer, uint32_t size) {
  buffer->pos = 0;

  if (size == buffer->size) {
    return 0;
  }

  if (buffer->data != NULL) {
    free(buffer->data);
    buffer->data = NULL;
  }

  buffer->checksum_dirty = true;

  if (size == 0) {
    buffer->size = 0;
    return 0;
  }

  buffer->data = malloc(size);

  if (buffer->data == NULL) {
    buffer->size = 0;
    ESP_LOGE(tag, "failed to allocate %d bytes for client buffer", size);
    return BLE_ATT_ERR_INSUFFICIENT_RES;
  }

  buffer->size = size;
  return 0;
}

static int ble_client_buffer_set_pos(struct ble_client_buffer *buffer, uint32_t pos) {
  if (pos > buffer->size) {
    buffer->pos = buffer->size;
  } else {
    buffer->pos = pos;
  }

  return 0;
}

struct sha_str { char hex[SHA256_DIGEST_LENGTH * 2 + 1]; } get_sha_str(
  const uint8_t checksum[SHA256_DIGEST_LENGTH]) {

  struct sha_str sha_str_i;
  for (unsigned int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
    sprintf(sha_str_i.hex + i * 2, "%02x", checksum[i]);
  }
  sha_str_i.hex[SHA256_DIGEST_LENGTH * 2] = '\0';

  return sha_str_i;
}

static int ble_client_buffer_get_checksum(struct ble_client_buffer *buffer,
    const uint8_t **out_ptr) {

  if (buffer->checksum_dirty) {
    int err = 0;
    mbedtls_sha256_context sha_ctx = {0};

    if (buffer->data == NULL && buffer->size != 0) {
      return BLE_ATT_ERR_UNLIKELY;
    }

    mbedtls_sha256_init(&sha_ctx);

    err = mbedtls_sha256_starts(&sha_ctx, 0);
    if (err) goto end;

    if (buffer->size != 0) {
      err = mbedtls_sha256_update(&sha_ctx, buffer->data, buffer->size);
      if (err) goto end;
    }

    err = mbedtls_sha256_finish(&sha_ctx, buffer->checksum);
    if (err) goto end;

  end:
    mbedtls_sha256_free(&sha_ctx);
    if (err != 0) {
      return BLE_ATT_ERR_UNLIKELY;
    }

    buffer->checksum_dirty = false;
    ESP_LOGI(tag, "computed SHA-256 of buffer: %s", get_sha_str(buffer->checksum).hex);
  }

  *out_ptr = buffer->checksum;
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

int ble_client_buffer_sha_dsc_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)attr_handle;

  struct ble_gatt_buffer_service *service = arg;

  const uint8_t *checksum = NULL;
  int err = ble_client_buffer_get_checksum(&service->buffer, &checksum);
  if (err != 0) {
    return err;
  }

  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_DSC: 
      if (os_mbuf_append(ctxt->om, checksum, SHA256_DIGEST_LENGTH) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
      }

      break;

    case BLE_GATT_ACCESS_OP_WRITE_DSC: {
      uint8_t client_checksum[SHA256_DIGEST_LENGTH];

      if (os_mbuf_copydata(ctxt->om, 0, SHA256_DIGEST_LENGTH, client_checksum) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
      }

      if (memcmp(checksum, client_checksum, SHA256_DIGEST_LENGTH) == 0) {
        ESP_LOGI(tag, "buffer checksum verified: %s", get_sha_str(client_checksum).hex);
        // TODO: signal that the buffer usable

      } else {
        ESP_LOGI(tag, "checksum verification failed:\n  client: %s\n  server: %s", 
            get_sha_str(checksum).hex, get_sha_str(client_checksum).hex);
      }

      break;
    }

    default:
      ESP_LOGE(tag, "illegal operation to buffer sha dsc with code: %d", ctxt->op);
      return BLE_ATT_ERR_UNLIKELY;
  }

  return 0;
}
