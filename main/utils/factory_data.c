#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "common.h"
#include "utils/factory_data.h"

static const char tag[] = APP_TAG "-factory-data";

#define FACTORY_PARTITION "factory_data"
#define FACTORY_NAMESPACE "factory"

static int factory_open(nvs_handle_t *handle) {
  int err = nvs_flash_init_partition(FACTORY_PARTITION);
  if (err != ESP_OK) {
    ESP_LOGE(tag, "factory nvs init: %s", esp_err_to_name(err));
    return err;
  }

  err = nvs_open_from_partition(FACTORY_PARTITION, FACTORY_NAMESPACE,
      NVS_READONLY, handle);
  if (err != ESP_OK) {
    ESP_LOGE(tag, "factory nvs open: %s", esp_err_to_name(err));
  }
  return err;
}

int factory_data_get_blob(const char *key, void *buf, size_t *len) {
  nvs_handle_t handle;
  int err = factory_open(&handle);
  if (err != ESP_OK) return err;

  err = nvs_get_blob(handle, key, buf, len);
  nvs_close(handle);
  if (err != ESP_OK) {
    ESP_LOGE(tag, "read %s: %s", key, esp_err_to_name(err));
  }
  return err;
}

int factory_data_get_str(const char *key, char *buf, size_t *len) {
  nvs_handle_t handle;
  int err = factory_open(&handle);
  if (err != ESP_OK) return err;

  err = nvs_get_str(handle, key, buf, len);
  nvs_close(handle);
  if (err != ESP_OK) {
    ESP_LOGE(tag, "read %s: %s", key, esp_err_to_name(err));
  }
  return err;
}
