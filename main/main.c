#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nimble/nimble_port_freertos.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_random.h>
#include <esp_log.h>

#include "common.h"
#include "ble/host.h"
#include "ml/infer.h"
#include "ppg/sensor.h"
#include "utils/worker.h"
#include "utils/ecdsa_utils.h"

#define FACTORY_PARTITION "factory_data"
#define FACTORY_NAMESPACE "factory"

static const char tag[] = APP_TAG "-main";

// Reads the factory identity and exercises the ECDSA keys: sign random data
// with the device private key and verify with the server public key.
static void factory_selftest(void) {
  int err = nvs_flash_init_partition(FACTORY_PARTITION);
  if (err != ESP_OK) {
    ESP_LOGE(tag, "failed to init factory nvs: %s", esp_err_to_name(err));
    return;
  }

  nvs_handle_t handle;
  err = nvs_open_from_partition(FACTORY_PARTITION, FACTORY_NAMESPACE,
      NVS_READONLY, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(tag, "failed to open factory namespace: %s", esp_err_to_name(err));
    return;
  }

  char serial[32];
  size_t serial_len = sizeof(serial);
  err = nvs_get_str(handle, "serial", serial, &serial_len);
  if (err != ESP_OK) {
    ESP_LOGE(tag, "failed to read serial: %s", esp_err_to_name(err));
    nvs_close(handle);
    return;
  }
  ESP_LOGI(tag, "device serial: %s", serial);

  uint8_t dev_priv[ECDSA_P256_PRIVKEY_LENGTH];
  uint8_t srv_pub[ECDSA_P256_PUBKEY_LENGTH];
  size_t len = sizeof(dev_priv);
  err = nvs_get_blob(handle, "dev_priv", dev_priv, &len);
  if (err == ESP_OK) {
    len = sizeof(srv_pub);
    err = nvs_get_blob(handle, "srv_pub", srv_pub, &len);
  }
  nvs_close(handle);
  if (err != ESP_OK) {
    ESP_LOGE(tag, "failed to read factory keys: %s", esp_err_to_name(err));
    return;
  }

  uint8_t data[32];
  esp_fill_random(data, sizeof(data));

  uint8_t sig[ECDSA_SIG_MAX_LENGTH];
  size_t sig_len = 0;
  err = ecdsa_sign(dev_priv, data, sizeof(data), sig, &sig_len);
  if (err) {
    ESP_LOGE(tag, "ecdsa sign failed: -0x%04x", (unsigned)-err);
    return;
  }

  err = ecdsa_verify(srv_pub, data, sizeof(data), sig, sig_len);
  if (err) {
    ESP_LOGW(tag, "ecdsa verify failed: -0x%04x (expected unless provisioned "
        "with the self-test identity)", (unsigned)-err);
  } else {
    ESP_LOGI(tag, "ecdsa sign/verify roundtrip ok");
  }
}

void app_main(void) {
  int err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  if (err != ESP_OK) {
    ESP_LOGE(tag, "failed to initialize nvs flash with error code %d ", err);
    return;
  }

  factory_selftest();

  err = ble_init();
  if (err != 0) return;

  err = worker_init();
  if (err != 0) return;

  xTaskCreate(worker_task, "Worker", 2048, NULL, 4, NULL);
  xTaskCreate(ppg_task, "PPG Sensor", 4096, NULL, 5, NULL);
  xTaskCreate(ml_task, "ML Infer", 4096, NULL, 5, NULL);

  nimble_port_freertos_init(ble_task);
}
