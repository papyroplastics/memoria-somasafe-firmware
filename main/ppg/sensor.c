#include <stddef.h>
#include <string.h>

#include <driver/uart.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "common.h"
#include "ppg/sensor.h"
#include "ppg/service.h"
#include "utils/ring_buffer.h"

static const char tag[] = APP_TAG "-ppg-sensor";

RING_BUFFER_STATIC(struct ppg_slice, ppg_ring, PPG_RING_CAPACITY);

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

static float uart_read_float(void) {
  float value;
  uint8_t *buf = (uint8_t *)&value;
  int received = 0;
  while (received < (int)sizeof(float)) {
    int n = uart_read_bytes(UART_PORT, buf + received,
                            sizeof(float) - received, portMAX_DELAY);
    if (n > 0) received += n;
  }
  return value;
}

float sensor_get_ppg(void) {
  return uart_read_float();
}

float sensor_get_acc(void) {
  return uart_read_float();
}

void ppg_task(void *param) {
  (void)param;

  if (uart_driver_install(UART_PORT, UART_BUF, UART_BUF, 0, NULL, 0) != ESP_OK ||
      uart_param_config(UART_PORT, &uart_config) != ESP_OK ||
      uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN,
                   UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
    ESP_LOGE(tag, "UART init failed");
    esp_restart();
  }

  struct ppg_slice *slice = NULL;
  uint16_t ppg_idx = 0;
  uint16_t acc_idx = 0;

  for (;;) {
    if (slice == NULL) {
      slice = ring_buffer_acquire_write(&ppg_ring);
      ppg_idx = 0;
      acc_idx = 0;
      slice->start_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    }

    // Each cycle: 2 PPG samples at 64 Hz, 1 ACC sample at 32 Hz
    slice->ppg[ppg_idx++] = sensor_get_ppg();
    slice->ppg[ppg_idx++] = sensor_get_ppg();
    slice->acc[acc_idx++] = sensor_get_acc();

    // Every second (64 PPG samples), notify the connected client
    if (ppg_idx % PPG_SAMPLE_RATE == 0) {
      uint16_t sec_ppg = ppg_idx - PPG_SAMPLE_RATE;
      uint16_t sec_acc = acc_idx - PPG_ACC_RATE;
      bool window_start = (sec_ppg == 0);
      ppg_notify_data(
        &slice->ppg[sec_ppg], PPG_SAMPLE_RATE,
        &slice->acc[sec_acc], PPG_ACC_RATE,
        window_start
      );
    }

    if (ppg_idx >= PPG_SLICE_PPG_COUNT) {
      slice->end_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
      ring_buffer_release_write(&ppg_ring);
      slice = NULL;
    }
  }

  ESP_LOGE(tag, "PPG task exited unexpectedly");
  esp_restart();
}
