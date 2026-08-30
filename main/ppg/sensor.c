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

void ppg_ring_wait_data() {
  ring_buffer_wait_data(&ppg_ring);
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

static void uart_read_all(uint8_t *buf, size_t len) {
  size_t received = 0;
  while (received < len) {
    int n = uart_read_bytes(UART_PORT, buf + received,
                            len - received, portMAX_DELAY);
    if (n > 0) received += (size_t)n;
  }
}

static void get_sample(float ppg[2], float acc[1]) {
  uint8_t buf[3 * sizeof(float)];
  uart_read_all(buf, sizeof(buf));
  memcpy(ppg, buf, 2 * sizeof(float));
  memcpy(acc, buf + 2 * sizeof(float), sizeof(float));
}

static uint32_t now_ms(void) {
  return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void get_second(struct ppg_slice *slice, uint16_t sec) {
  uint16_t ppg_base = sec * PPG_SAMPLE_RATE;
  uint16_t acc_base = sec * PPG_ACC_RATE;
  for (uint16_t i = 0; i < PPG_SAMPLE_RATE; i += 2) {
    get_sample(&slice->ppg[ppg_base + i], &slice->acc[acc_base + i / 2]);
  }
}

static void send_second(struct transaction_state *tx, const struct ppg_slice *slice,
                        uint16_t sec, bool window_start, bool window_end) {
  ppg_data_notify_send(tx,
      &slice->ppg[sec * PPG_SAMPLE_RATE], PPG_SAMPLE_RATE,
      &slice->acc[sec * PPG_ACC_RATE], PPG_ACC_RATE,
      window_start, window_end, slice->sequence_n,
      slice->start_ms, slice->end_ms);
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

  uint32_t next_sequence_n = 0;

  // One transaction per slice; reused across slices so transaction ids advance.
  struct transaction_state ppg_tx = TRANSACTION_STATE_INIT;

  for (;;) {
    struct ppg_slice *slice = ring_buffer_acquire_write(&ppg_ring);
    slice->sequence_n = next_sequence_n++;
    slice->start_ms = now_ms();

    // First second opens the transaction.
    get_second(slice, 0);
    send_second(&ppg_tx, slice, 0, true, false);

    // Middle seconds are plain continuations.
    for (uint16_t sec = 1; sec < PPG_SLICE_SECONDS - 1; sec++) {
      get_second(slice, sec);
      send_second(&ppg_tx, slice, sec, false, false);
    }

    // Last second closes the transaction and carries the acquisition time.
    uint16_t last = PPG_SLICE_SECONDS - 1;
    get_second(slice, last);
    slice->end_ms = now_ms();
    send_second(&ppg_tx, slice, last, false, true);

    ESP_LOGI(tag, "finished writing sample %d", slice->sequence_n);
    ring_buffer_release_write(&ppg_ring);
  }

  ESP_LOGE(tag, "PPG task exited unexpectedly");
  esp_restart();
}
