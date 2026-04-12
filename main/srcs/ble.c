#include "common.h"
#include "ble.h"
#include "gap.h"
#include "gatt.h"

#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>

static const char tag[] = "nimble-example-ble";

const char device_name[] = "Heart rate monitor";
const char device_name_short[] = "HRM";
const uint16_t device_appearance = 0x0340;  // heart

const ble_uuid16_t heart_rate_svc_uuid = BLE_UUID16_INIT(0x180D);
const ble_uuid16_t heart_rate_chr_uuid = BLE_UUID16_INIT(0x2A37);

static void on_stack_reset(int reason) {
  ESP_LOGI(tag, "NimBLE stack reset with reason %d", reason);
}

static void on_stack_sync(void) {
  prepare_adv();
  start_adv();
}

int ble_init() {
  // NimBLE stack initialization
  int err = nimble_port_init();
  if (err != ESP_OK) {
    ESP_LOGE(tag, "failed to initialize NimBLE stack");
    return 1;
  }

  err = gap_init();
  if (err != 0) {
    ESP_LOGE(tag, "failed to initialize GAP service");
    return 1;
  }

  err = gatt_init();
  if (err != 0) {
    ESP_LOGE(tag, "failed to initialize GATT service");
    return 1;
  }

  ble_hs_cfg.reset_cb = on_stack_reset;
  ble_hs_cfg.sync_cb = on_stack_sync;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

  return 0;
}

void ble_task(void *param) {
  ESP_LOGI(tag, "starting NimBLE task");
  for (;;) {
    nimble_port_run();
  }
}

