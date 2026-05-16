#include <esp_log.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "ppg/sensor.h"
#include "ble/host.h"
#include "ble/gatt.h"

static const char tag[] = "nimble-example-ppg";
static uint8_t heart_rate;

uint8_t ppg_get_hr(void) { return heart_rate; }

static void signal_hr(void* arg) {
  gatt_hr_chr_update();
}

void ppg_task(void *param) {
  for(;;) {
    heart_rate = 60 + (uint8_t)(esp_random() % 21);
    ESP_LOGI(tag, "HR updated to %d", heart_rate);

    push_work_to_nimple_host_task(signal_hr, NULL);

    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }

  ESP_LOGE(tag, "PPG task exiting unexpectedly, restarting system");
  esp_restart();
}
