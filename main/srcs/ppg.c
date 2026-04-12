#include "common.h"
#include "ppg.h"
#include "gatt.h"

#include <esp_random.h>

static const char tag[] = "nimble-example-ppg";
static uint8_t heart_rate;

uint8_t ppg_get_hr(void) { return heart_rate; }

void ppg_task(void *param) {
  for(;;) {
    heart_rate = 60 + (uint8_t)(esp_random() % 21);


    int err = gatt_hr_attr_signal();
    if (err == 0) {
      ESP_LOGI(tag, "HR indication sent", heart_rate);
    } else {
      ESP_LOGI(tag, "HR updated to %d", heart_rate);
    }

    /* Sleep */
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }

  esp_restart();
}
