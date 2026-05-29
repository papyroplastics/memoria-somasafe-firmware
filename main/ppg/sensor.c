#include <math.h>
#include <stddef.h>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "common.h"
#include "ppg/sensor.h"
#include "esp_system.h"
#include "ppg/service.h"
#include "ble/host.h"
#include "ble/gatt.h"
#include "utils/ring_buffer.h"

static const char tag[] = APP_TAG "-ppg-sensor";
static uint8_t heart_rate;

uint8_t ppg_get_hr(void) {
  return heart_rate;
}

RING_BUFFER_STATIC(ppg_ring, struct ppg_slice, PPG_RING_CAPACITY);

bool ppg_ring_acquire_read(struct ppg_slice **slice_out) {
  return ring_buffer_acquire_read(&ppg_ring, (void **)slice_out);
}

void ppg_ring_release_read(void) {
  ring_buffer_release_read(&ppg_ring);
}

void ppg_task(void *param) {
  (void)param;

  const float step = (2.0f * (float)M_PI) / (float)PPG_SLICE_SAMPLES;
  struct ppg_slice *slice = NULL;
  uint16_t sample_idx = 0;
  TickType_t last_hr_tick = 0;

  for (;;) {
    if (slice == NULL) {
      slice = ring_buffer_acquire_write(&ppg_ring);
      sample_idx = 0;
      slice->sample_count = 0;
      slice->start_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    }

    float value = (float)sample_idx * step;
    slice->samples[sample_idx] = value;
    slice->sample_count = (uint16_t)(sample_idx + 1);
    sample_idx++;

    TickType_t now = xTaskGetTickCount();
    if (sample_idx >= PPG_SLICE_SAMPLES) {
      slice->end_ms = (uint32_t)(now * portTICK_PERIOD_MS);
      ring_buffer_release_write(&ppg_ring);
      slice = NULL;
    }

    if (now - last_hr_tick >= pdMS_TO_TICKS(1000)) {
      last_hr_tick = now;
      heart_rate = (uint8_t)(60 + ((sample_idx / PPG_SAMPLE_RATE_HZ) % 21));
      ble_gatts_chr_updated(hr_chr_handle);
    }


    vTaskDelay(pdMS_TO_TICKS(1000 / PPG_SAMPLE_RATE_HZ));
  }

  ESP_LOGE(tag, "PPG task exited unexpectedly");
  esp_restart();
}
