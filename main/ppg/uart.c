#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_err.h>
#include <esp_log.h>

#include <hal/uart_types.h>
#include <esp_intr_alloc.h>
#include <driver/uart.h>
#include <driver/gpio.h>

#include "common.h"
#include "ppg/uart.h"

static const char tag[] = APP_TAG "-ble-service";

static uart_port_t uart_port = UART_NUM_0;
const uint32_t uart_buffer_size = (1024 * 2);

static uart_config_t uart_config = {
  .baud_rate  = 115200,
  .data_bits  = UART_DATA_8_BITS,
  .parity     = UART_PARITY_DISABLE,
  .stop_bits  = UART_STOP_BITS_1,
  .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
  .source_clk = UART_SCLK_DEFAULT,
};

void echo_task(void *arg) {
  QueueHandle_t uart_queue;
  esp_err_t err = uart_driver_install(uart_port, uart_buffer_size, uart_buffer_size, 10, &uart_queue, ESP_INTR_FLAG_IRAM);
  if (err != 0) {
    ESP_LOGE(tag, "unable to install UART driver");
    return;
  }

  if (uart_param_config(uart_port, &uart_config)) {
    ESP_LOGE(tag, "unable to configure UART parameters");
    return;
  }

  if (uart_set_pin(uart_port, 43, 44, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE)) {
    ESP_LOGE(tag, "unable to set UART pins");
    return;
  }

  uint8_t *data = (uint8_t *) malloc(uart_buffer_size);
  if (data == NULL) {
    ESP_LOGE(tag, "Unable to allocate buffer for reading");
    return;
  }

  while (1) {
    int len = uart_read_bytes(uart_port, data, (uart_buffer_size - 1), 20 / portTICK_PERIOD_MS);
    if (len) {
      data[len] = '\0';
      ESP_LOGI(tag, "read string with length %d: %s", len, (char *) data);
    }
    uart_write_bytes(uart_port, data, len);
  }
}

