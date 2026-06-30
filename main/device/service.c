#include <stddef.h>
#include <stdint.h>

#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <host/ble_att.h>
#include <host/ble_hs.h>

#include "common.h"
#include "ble/gap.h"
#include "ble/gatt.h"
#include "ble/host.h"
#include "ble/client_buffer.h"
#include "device/service.h"
#include "utils/ecdsa_utils.h"
#include "ble/notif_transaction.h"

static const char tag[] = APP_TAG "-device";

#define FACTORY_PARTITION "factory_data"
#define FACTORY_NAMESPACE "factory"

struct ble_client_buffer device_sign_buffer = BLE_CLIENT_BUFFER_INIT("sign");

// Each signature is a complete single-call transaction; the state persists
// across calls only to advance the transaction id.
static struct transaction_state device_sign_tx = TRANSACTION_STATE_INIT;

static int load_device_privkey(uint8_t priv[ECDSA_P256_PRIVKEY_LENGTH]) {
  int err = nvs_flash_init_partition(FACTORY_PARTITION);
  if (err != ESP_OK) {
    ESP_LOGE(tag, "failed to init factory nvs: %s", esp_err_to_name(err));
    return err;
  }

  nvs_handle_t handle;
  err = nvs_open_from_partition(FACTORY_PARTITION, FACTORY_NAMESPACE,
      NVS_READONLY, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(tag, "failed to open factory namespace: %s", esp_err_to_name(err));
    return err;
  }

  size_t len = ECDSA_P256_PRIVKEY_LENGTH;
  err = nvs_get_blob(handle, "dev_priv", priv, &len);
  nvs_close(handle);
  if (err != ESP_OK) {
    ESP_LOGE(tag, "failed to read device private key: %s", esp_err_to_name(err));
  }
  return err;
}

// Reads the NUL-terminated serial string into `serial` (capacity `cap`) and
// stores its length (excluding the terminator) in `*len_out`.
static int load_device_serial(char *serial, size_t cap, size_t *len_out) {
  int err = nvs_flash_init_partition(FACTORY_PARTITION);
  if (err != ESP_OK) {
    ESP_LOGE(tag, "failed to init factory nvs: %s", esp_err_to_name(err));
    return err;
  }

  nvs_handle_t handle;
  err = nvs_open_from_partition(FACTORY_PARTITION, FACTORY_NAMESPACE,
      NVS_READONLY, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(tag, "failed to open factory namespace: %s", esp_err_to_name(err));
    return err;
  }

  size_t len = cap;
  err = nvs_get_str(handle, "serial", serial, &len);
  nvs_close(handle);
  if (err != ESP_OK) {
    ESP_LOGE(tag, "failed to read device serial: %s", esp_err_to_name(err));
    return err;
  }

  *len_out = len > 0 ? len - 1 : 0;  // nvs_get_str counts the NUL terminator
  return ESP_OK;
}

int device_serial_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)attr_handle;
  (void)arg;

  if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
    return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
  }

  char serial[32];
  size_t len = 0;
  if (load_device_serial(serial, sizeof(serial), &len) != ESP_OK) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  if (os_mbuf_append(ctxt->om, serial, len) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  return 0;
}

static void device_sign_notify_send(const uint8_t *sig, size_t sig_len) {
  if (!device_sign_chr_notify) return;

  uint16_t conn = ble_gap_get_conn_handle();
  if (conn == BLE_HS_CONN_HANDLE_NONE) return;

  ASSERT_ENCRYPYED()

  uint16_t mtu = ble_att_mtu(conn);
  if (mtu < BLE_ATT_MTU_DFLT) return;

  uint16_t max_payload = mtu - 3;

  const struct packet_segment segments[] = {
    { sig, sig_len },
  };

  notif_transaction_send(&device_sign_tx, conn, device_sign_chr_handle,
      segments, 1, max_payload, /*start=*/true, /*end=*/true);
}

void device_sign_task(void *param) {
  (void)param;

  uint8_t priv[ECDSA_P256_PRIVKEY_LENGTH];
  if (load_device_privkey(priv) != ESP_OK) {
    ESP_LOGE(tag, "signing disabled: device key unavailable");
    vTaskDelete(NULL);
    return;
  }

  for (;;) {
    // Blocks until the client uploads a payload and flips the buffer to READY.
    ble_client_buffer_lock(&device_sign_buffer);

    uint32_t payload_len = device_sign_buffer.size;
    uint8_t sig[ECDSA_SIG_MAX_LENGTH];
    size_t sig_len = 0;
    int err = ecdsa_sign(priv, device_sign_buffer.data, payload_len,
        sig, &sig_len);

    // Consume the payload: drop back to NOT_READY immediately (releasing the
    // mutex and notifying the client) so the next lock blocks on a new upload.
    ble_client_buffer_reset(&device_sign_buffer);

    if (err) {
      ESP_LOGE(tag, "failed to sign payload: -0x%04x", (unsigned)-err);
      continue;
    }

    ESP_LOGI(tag, "signed %u-byte payload into a %u-byte signature",
        (unsigned)payload_len, (unsigned)sig_len);
    device_sign_notify_send(sig, sig_len);
  }

  ESP_LOGE(tag, "device sign task exited unexpectedly");
  esp_restart();
}
