#include <stddef.h>
#include <string.h>

#include <driver/uart.h>
#include <esp_log.h>
#include <esp_random.h>
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

static const uint8_t uart_marker[4] = { 0xAA, 0xBB, 0xCC, 0xDD };

static void uart_read_all(uint8_t *buf, size_t len) {
  size_t received = 0;
  while (received < len) {
    int n = uart_read_bytes(UART_PORT, buf + received,
                            len - received, portMAX_DELAY);
    if (n > 0) received += (size_t)n;
  }
}

static void uart_write_all(const uint8_t *buf, size_t len) {
  uart_write_bytes(UART_PORT, buf, len);
}

// Flush stale channel data before streaming: send marker + our nonce, scan
// the incoming stream for the same marker + nonce echoed back, then echo the
// host's nonce that follows it.
static void uart_handshake(void) {
  uint8_t expect[8];
  uint32_t nonce = esp_random();
  memcpy(expect, uart_marker, sizeof(uart_marker));
  memcpy(expect + sizeof(uart_marker), &nonce, sizeof(nonce));

  uart_write_all(expect, sizeof(expect));

  size_t matched = 0;
  while (matched < sizeof(expect)) {
    uint8_t b;
    uart_read_all(&b, 1);
    if (b == expect[matched]) matched++;
    else matched = (b == expect[0]) ? 1 : 0;
  }

  uint8_t host_nonce[4];
  uart_read_all(host_nonce, sizeof(host_nonce));
  uart_write_all(host_nonce, sizeof(host_nonce));

  ESP_LOGI(tag, "UART handshake complete");
}

// Read one sample cycle (2 PPG + 1 ACC floats + postfix byte) and ack it by
// echoing the postfix byte back.
static void get_sample(float ppg[2], float acc[1]) {
  uint8_t buf[3 * sizeof(float) + 1];
  uart_read_all(buf, sizeof(buf));
  memcpy(ppg, buf, 2 * sizeof(float));
  memcpy(acc, buf + 2 * sizeof(float), sizeof(float));
  uart_write_all(&buf[sizeof(buf) - 1], 1);
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

  uart_handshake();

  struct ppg_slice *slice = NULL;
  uint16_t ppg_idx = 0;
  uint16_t acc_idx = 0;
  uint32_t next_sequence_n = 0;

  for (;;) {
    if (slice == NULL) {
      slice = ring_buffer_acquire_write(&ppg_ring);
      ppg_idx = 0;
      acc_idx = 0;
      slice->sequence_n = next_sequence_n++;
      slice->start_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    }

    // Each cycle: 2 PPG samples at 64 Hz, 1 ACC sample at 32 Hz
    get_sample(&slice->ppg[ppg_idx], &slice->acc[acc_idx]);
    ppg_idx += 2;
    acc_idx += 1;

    // Every second (64 PPG samples), notify the connected client
    if (ppg_idx % PPG_SAMPLE_RATE == 0) {
      uint16_t sec_ppg = ppg_idx - PPG_SAMPLE_RATE;
      uint16_t sec_acc = acc_idx - PPG_ACC_RATE;
      bool window_start = (sec_ppg == 0);
      ppg_data_notify_send(
        &slice->ppg[sec_ppg], PPG_SAMPLE_RATE,
        &slice->acc[sec_acc], PPG_ACC_RATE,
        window_start, slice->sequence_n
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
