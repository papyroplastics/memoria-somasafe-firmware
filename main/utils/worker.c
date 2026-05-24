#include <stdlib.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_log.h>

#include "common.h"
#include "esp_system.h"
#include "utils/worker.h"

static const char tag[] = APP_TAG "-worker";

#define WORKER_QUEUE_LENGTH 8

typedef struct {
  worker_task_cb cb;
  void *arg;
} worker_item_t;

static QueueHandle_t worker_queue;

int worker_init(void) {
  if (worker_queue != NULL) {
    return 0;
  }

  worker_queue = xQueueCreate(WORKER_QUEUE_LENGTH, sizeof(worker_item_t *));
  if (worker_queue == NULL) {
    ESP_LOGE(tag, "failed to create worker queue");
    return 1;
  }

  return 0;
}

int worker_queue_push_task(worker_task_cb cb, void *arg) {
  if (worker_queue == NULL) {
    ESP_LOGE(tag, "worker queue not initialized");
    return 1;
  }

  worker_item_t *item = malloc(sizeof(worker_item_t));
  if (item == NULL) {
    return 1;
  }

  item->cb = cb;
  item->arg = arg;

  if (xQueueSend(worker_queue, &item, 0) != pdTRUE) {
    free(item);
    return 1;
  }

  return 0;
}

void worker_task(void *param) {
  (void)param;

  if (worker_queue == NULL) {
    if (worker_init() != 0) {
      ESP_LOGE(tag, "worker queue init failed");
      esp_restart();
    }
  }

  for (;;) {
    worker_item_t *item = NULL;
    if (xQueueReceive(worker_queue, &item, portMAX_DELAY) == pdTRUE) {
      if (item != NULL && item->cb != NULL) {
        item->cb(item->arg);
      }
      free(item);
    }
  }

  ESP_LOGE(tag, "worker task exited unexpectedly");
  esp_restart();
}
