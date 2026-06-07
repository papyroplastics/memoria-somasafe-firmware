#include <stddef.h>

#include <driver/uart.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "common.h"
#include "ppg/sensor.h"
#include "utils/ring_buffer.h"

static const char tag[] = APP_TAG "-ppg-sensor";

uint8_t ppg_get_hr(void) { return 0; }

RING_BUFFER_STATIC(ppg_ring, struct ppg_slice, PPG_RING_CAPACITY);

bool ppg_ring_acquire_read(struct ppg_slice **slice_out) {
  return ring_buffer_acquire_read(&ppg_ring, (void **)slice_out);
}

void ppg_ring_release_read(void) {
  ring_buffer_release_read(&ppg_ring);
}

#define UART_PORT   UART_NUM_0
#define UART_TX_PIN 43
#define UART_RX_PIN 44
#define UART_BUF    (1024 * 2)

static const uart_config_t uart_config = {
  .baud_rate  = 115200,
  .data_bits  = UART_DATA_8_BITS,
  .parity     = UART_PARITY_DISABLE,
  .stop_bits  = UART_STOP_BITS_1,
  .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
  .source_clk = UART_SCLK_DEFAULT,
};


void ppg_task(void *param) {
  (void)param;

  if (uart_driver_install(UART_PORT, UART_BUF, UART_BUF, 0, NULL, 0) != ESP_OK ||
      uart_param_config(UART_PORT, &uart_config) != ESP_OK ||
      uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
    ESP_LOGE(tag, "UART init failed");
    esp_restart();
  }

  const size_t feature_bytes = PPG_N_FEATURES * sizeof(float);
  struct ppg_slice *slice = NULL;
  uint16_t window_idx = 0;

  for (;;) {
    if (slice == NULL) {
      slice = ring_buffer_acquire_write(&ppg_ring);
      window_idx = 0;
      slice->sample_count = 0;
      slice->start_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    }

    uint8_t *dst = (uint8_t *)&slice->samples[window_idx * PPG_N_FEATURES];
    int received = 0;
    while (received < (int)feature_bytes) {
      int n = uart_read_bytes(UART_PORT, dst + received, feature_bytes - received, portMAX_DELAY);
      if (n > 0) received += n;
    }

    window_idx++;
    slice->sample_count = window_idx;

    if (window_idx >= PPG_SLICE_WINDOWS) {
      slice->end_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
      ring_buffer_release_write(&ppg_ring);
      slice = NULL;
    }
  }

  ESP_LOGE(tag, "PPG task exited unexpectedly");
  esp_restart();
}
