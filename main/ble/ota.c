#include <string.h>

#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <host/ble_att.h>
#include <host/ble_gatt.h>

#include "common.h"
#include "ble/gatt.h"
#include "ble/host.h"
#include "ble/ota.h"
#include "utils/ecdsa_utils.h"
#include "utils/factory_data.h"
#include "utils/sha_utils.h"
#include "utils/worker.h"

static const char tag[] = APP_TAG "-ota";

#define RESTART_DELAY_MS 500

static enum ble_ota_state state = BLE_OTA_STATE_IDLE;
static esp_ota_handle_t update_handle;
static const esp_partition_t *update_partition;
static uint32_t image_len;
static struct sha256_stream image_sha;
static uint8_t signature[ECDSA_SIG_MAX_LENGTH];
static size_t signature_len;

// All state transitions run on the NimBLE host task (ATT access callbacks),
// so no locking is needed.
static void set_state(enum ble_ota_state new_state) {
  state = new_state;
  ble_gatts_chr_updated(ota_state_chr_handle);
}

static void restart_work(void *arg) {
  (void)arg;
  vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY_MS));
  esp_restart();
}

static int fail_update(const char *reason, int err) {
  ESP_LOGE(tag, "update failed (%s): %d", reason, err);
  if (state == BLE_OTA_STATE_RECEIVING) {
    esp_ota_abort(update_handle);
  }
  set_state(BLE_OTA_STATE_ERROR);
  return BLE_ATT_ERR_UNLIKELY;
}

static int ota_start(void) {
  if (state == BLE_OTA_STATE_VERIFYING) {
    return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
  }
  if (state == BLE_OTA_STATE_RECEIVING) {
    ESP_LOGW(tag, "client restarted the update");
    esp_ota_abort(update_handle);
    state = BLE_OTA_STATE_IDLE;
  }

  update_partition = esp_ota_get_next_update_partition(NULL);
  if (update_partition == NULL) {
    return fail_update("no update partition", 0);
  }

  int err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES,
      &update_handle);
  if (err != ESP_OK) {
    return fail_update("esp_ota_begin", err);
  }

  err = sha256_stream_begin(&image_sha);
  if (err) {
    esp_ota_abort(update_handle);
    return fail_update("sha256 begin", err);
  }

  image_len = 0;
  signature_len = 0;

  ESP_LOGI(tag, "update started, writing to partition \"%s\"",
      update_partition->label);
  set_state(BLE_OTA_STATE_RECEIVING);
  return 0;
}

static int ota_abort(void) {
  if (state == BLE_OTA_STATE_VERIFYING) {
    return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
  }
  if (state == BLE_OTA_STATE_RECEIVING) {
    ESP_LOGW(tag, "client aborted the update");
    esp_ota_abort(update_handle);
  }
  set_state(BLE_OTA_STATE_IDLE);
  return 0;
}

static int ota_finalize(void) {
  if (state != BLE_OTA_STATE_RECEIVING || image_len == 0 || signature_len == 0) {
    return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
  }

  uint8_t digest[SHA256_DIGEST_LENGTH];
  int err = sha256_stream_final(&image_sha, digest);
  if (err) {
    return fail_update("sha256 final", err);
  }

  uint8_t srv_pub[ECDSA_P256_PUBKEY_LENGTH];
  size_t pub_len = sizeof(srv_pub);
  err = factory_data_get_blob("srv_pub", srv_pub, &pub_len);
  if (err != ESP_OK) {
    return fail_update("read srv_pub", err);
  }

  err = ecdsa_verify_digest(srv_pub, digest, signature, signature_len);
  if (err) {
    return fail_update("signature verification", err);
  }

  err = esp_ota_end(update_handle);
  state = BLE_OTA_STATE_IDLE;  // handle is freed regardless of the result
  if (err != ESP_OK) {
    return fail_update("esp_ota_end", err);
  }

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    return fail_update("esp_ota_set_boot_partition", err);
  }

  ESP_LOGI(tag, "update verified (%lu bytes), restarting into \"%s\"",
      (unsigned long)image_len, update_partition->label);
  set_state(BLE_OTA_STATE_VERIFYING);

  if (worker_queue_push_task(restart_work, NULL) != 0) {
    ESP_LOGE(tag, "failed to schedule restart, restarting inline");
    esp_restart();
  }
  return 0;
}

int ble_ota_version_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)attr_handle;
  (void)arg;

  if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
    return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
  }

  uint16_t interface_version = BLE_INTERFACE_VERSION;
  const esp_app_desc_t *desc = esp_app_get_description();
  size_t version_len = strnlen(desc->version, sizeof(desc->version));

  if (os_mbuf_append(ctxt->om, &interface_version, sizeof(interface_version)) != 0 ||
      os_mbuf_append(ctxt->om, desc->version, version_len) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  return 0;
}

int ble_ota_data_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)attr_handle;
  (void)arg;

  static uint8_t chunk[BLE_ATT_ATTR_MAX_LEN];

  if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
  }
  if (state != BLE_OTA_STATE_RECEIVING) {
    return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
  }

  uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
  if (len == 0) {
    return 0;
  }
  if (len > sizeof(chunk) ||
      os_mbuf_copydata(ctxt->om, 0, len, chunk) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  int err = esp_ota_write(update_handle, chunk, len);
  if (err != ESP_OK) {
    return fail_update("esp_ota_write", err);
  }

  err = sha256_stream_update(&image_sha, chunk, len);
  if (err) {
    return fail_update("sha256 update", err);
  }

  image_len += len;
  return 0;
}

int ble_ota_signature_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)attr_handle;
  (void)arg;

  if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
  }
  if (state != BLE_OTA_STATE_RECEIVING) {
    return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
  }

  uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
  if (signature_len + len > sizeof(signature)) {
    return BLE_ATT_ERR_INSUFFICIENT_RES;
  }
  if (os_mbuf_copydata(ctxt->om, 0, len, signature + signature_len) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  signature_len += len;
  return 0;
}

int ble_ota_state_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)attr_handle;
  (void)arg;

  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR: {
      uint8_t value = state;
      if (os_mbuf_append(ctxt->om, &value, 1) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
      }
      return 0;
    }

    case BLE_GATT_ACCESS_OP_WRITE_CHR: {
      uint8_t value = 0;
      if (os_mbuf_copydata(ctxt->om, 0, 1, &value) != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      }

      switch (value) {
        case BLE_OTA_STATE_IDLE:
          return ota_abort();
        case BLE_OTA_STATE_RECEIVING:
          return ota_start();
        case BLE_OTA_STATE_VERIFYING:
          return ota_finalize();
        default:
          ESP_LOGE(tag, "client wrote illegal ota state %d", value);
          return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
      }
    }

    default:
      ESP_LOGE(tag, "illegal operation on ota state chr with code: %d", ctxt->op);
      return BLE_ATT_ERR_UNLIKELY;
  }
}
