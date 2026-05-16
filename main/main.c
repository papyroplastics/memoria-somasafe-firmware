#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <esp_log.h>

#include "common.h"
#include "ble/host.h"
#include "ppg/sensor.h"

static const char tag[] = APP_TAG "-main";

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

  err = ble_init();
  if (err != 0) return;

  xTaskCreate(ble_task, "NimBLE Host", CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE, NULL, configMAX_PRIORITIES, NULL);
  xTaskCreate(ppg_task, "PPG Sensor", 1024, NULL, 5, NULL);
}
