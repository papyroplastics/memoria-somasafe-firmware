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

static const char tag[] = APP_TAG "-ppg";
static uint8_t heart_rate;

uint8_t ppg_get_hr(void) {
  return heart_rate;
}

static struct ppg_ring_buffer ppg_ring = {
  .mutex = PTHREAD_MUTEX_INITIALIZER,
  .read_mutex = PTHREAD_MUTEX_INITIALIZER,
  .write_mutex = PTHREAD_MUTEX_INITIALIZER,
  .oldest_idx = 0,
  .newest_idx = 0,
  .read_idx = SIZE_MAX,
  .has_data = false,
};

static size_t next_snapshot_idx(size_t idx) {
  return (idx + 1) % PPG_RING_CAPACITY;
}

static struct ppg_snapshot *ppg_ring_acquire_write(void) {
  pthread_mutex_lock(&ppg_ring.mutex);

  size_t next_idx = 0;
  if (!ppg_ring.has_data) {
    ppg_ring.has_data = true;
    ppg_ring.oldest_idx = 0;
    ppg_ring.newest_idx = 0;
  } else {
    next_idx = next_snapshot_idx(ppg_ring.newest_idx);

    if (next_idx == ppg_ring.oldest_idx) {
      ppg_ring.oldest_idx = next_snapshot_idx(ppg_ring.oldest_idx);
    }

    if (next_idx == ppg_ring.read_idx) {
      pthread_mutex_lock(&ppg_ring.read_mutex);
      ppg_ring.read_idx = SIZE_MAX;
      pthread_mutex_unlock(&ppg_ring.read_mutex);
    }

    ppg_ring.newest_idx = next_idx;
  }

  pthread_mutex_lock(&ppg_ring.write_mutex);
  pthread_mutex_unlock(&ppg_ring.mutex);
  return &ppg_ring.snapshots[ppg_ring.newest_idx];
}

static void ppg_ring_release_write(void) {
  pthread_mutex_unlock(&ppg_ring.write_mutex);
}

bool ppg_ring_acquire_read(struct ppg_snapshot **snapshot_out) {
  if (snapshot_out == NULL) {
    return false;
  }

  pthread_mutex_lock(&ppg_ring.mutex);
  if (!ppg_ring.has_data) {
    pthread_mutex_unlock(&ppg_ring.mutex);
    return false;
  }

  size_t candidate = ppg_ring.oldest_idx;
  if (ppg_ring.read_idx != SIZE_MAX) {
    candidate = next_snapshot_idx(ppg_ring.read_idx);
  }

  if (candidate == ppg_ring.newest_idx) {
    pthread_mutex_unlock(&ppg_ring.mutex);
    return false;
  }

  ppg_ring.read_idx = candidate;
  pthread_mutex_lock(&ppg_ring.read_mutex);
  pthread_mutex_unlock(&ppg_ring.mutex);

  *snapshot_out = &ppg_ring.snapshots[candidate];
  return true;
}

void ppg_ring_release_read(void) {
  pthread_mutex_unlock(&ppg_ring.read_mutex);
}

void ppg_task(void *param) {
  (void)param;

  const float step = (2.0f * (float)M_PI) / (float)PPG_SNAPSHOT_SAMPLES;
  struct ppg_snapshot *snapshot = NULL;
  uint16_t sample_idx = 0;
  TickType_t last_hr_tick = 0;

  for (;;) {
    if (snapshot == NULL) {
      snapshot = ppg_ring_acquire_write();
      sample_idx = 0;
      snapshot->sample_count = 0;
      snapshot->start_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    }

    float value = (float)sample_idx * step;
    snapshot->samples[sample_idx] = value;
    snapshot->sample_count = (uint16_t)(sample_idx + 1);
    sample_idx++;

    TickType_t now = xTaskGetTickCount();
    if (sample_idx >= PPG_SNAPSHOT_SAMPLES) {
      snapshot->end_ms = (uint32_t)(now * portTICK_PERIOD_MS);
      ppg_ring_release_write();
      snapshot = NULL;
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
