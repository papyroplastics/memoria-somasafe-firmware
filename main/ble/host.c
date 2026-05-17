#include <stdint.h>
#include <esp_log.h>
#include <host/ble_hs.h>
#include <nimble/nimble_port.h>

#include "common.h"
#include "ble/host.h"
#include "ble/gatt.h"
#include "ble/gap.h"

static const char tag[] = APP_TAG "-ble";

static const uint8_t nimble_port_max_retries = 10;

const uint16_t device_appearance = 0x0340;  // heart

const ble_uuid16_t hr_svc_uuid = BLE_UUID16_INIT(0x180D);
const ble_uuid16_t hr_chr_uuid = BLE_UUID16_INIT(0x2A37);

const ble_uuid128_t model_svc_uuid = BLE_UUID128_INIT(
    0x38, 0x27, 0x43, 0xd4, 0xda, 0xb7, 0x43, 0xfe,
    0x92, 0x24, 0x43, 0x75, 0x40, 0x38, 0x52, 0xa4,
);
const ble_uuid128_t model_chr_uuid = BLE_UUID128_INIT(
    0x5a, 0xf2, 0x87, 0x8c, 0xa3, 0x6f, 0x4d, 0xc0,
    0x86, 0x8a, 0xcb, 0x1d, 0xa6, 0xf3, 0x04, 0x8f,
);
const ble_uuid128_t model_size_dsc_uuid = BLE_UUID128_INIT(
    0x85, 0x25, 0x7e, 0x0a, 0x4b, 0xae, 0x48, 0xab,
    0x87, 0x55, 0x34, 0xbf, 0xc8, 0x84, 0x73, 0x23,
);
const ble_uuid128_t model_pos_dsc_uuid = BLE_UUID128_INIT(
    0xae, 0x4b, 0x37, 0x79, 0x20, 0x0f, 0x4a, 0x48,
    0xaf, 0x57, 0xbe, 0xb6, 0x9c, 0x5c, 0x56, 0xf5,
);
const ble_uuid128_t model_sha_dsc_uuid = BLE_UUID128_INIT(
    0xa0, 0x54, 0xbc, 0x59, 0x48, 0xc6, 0x45, 0x95,
    0xa0, 0x8f, 0xea, 0xf2, 0x8d, 0x87, 0x7e, 0x71,
);

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

  ble_hs_cfg.reset_cb = on_stack_reset;
  ble_hs_cfg.sync_cb = on_stack_sync;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

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

  return 0;
}

void ble_task(void *param) {
  ESP_LOGI(tag, "starting NimBLE task");
  for (uint8_t i = 0; i < nimble_port_max_retries; i++) {
    nimble_port_run();
    ESP_LOGE(tag, "nimble_port_run() returned unexpectedly, retrying");
  }

  ESP_LOGE(tag, "nimble_port_run() returned after %d retries, restarting system", nimble_port_max_retries);
  esp_restart();
}

typedef struct {
    void (*cb)(void*);
    void *arg;
} ble_work_t;

static void handle_work(struct ble_npl_event *ev) {
    ble_work_t *work = ble_npl_event_get_arg(ev);
    work->cb(work->arg);
    free(work);
    free(ev);
}

/**
 * for those APIs see: 
 * - $IDF_PATH/components/bt/host/nimble/nimble/porting/npl/freertos/include/nimble/nimble_npl_os.h
 * - $IDF_PATH/components/bt/host/nimble/nimble/porting/npl/freertos/src/npl_os_freertos.c
 */
int push_work_to_nimple_host_task(void (*cb)(void*), void *arg) {
  ble_work_t *work = malloc(sizeof(ble_work_t));
  if (work == NULL) return 1;

  work->cb = cb;
  work->arg = arg;

  struct ble_npl_event *ev = malloc(sizeof(struct ble_npl_event));
  if (ev == NULL) {
    free(work);
    return 1;
  }
  ble_npl_event_init(ev, handle_work, work);
  ble_npl_eventq_put(nimble_port_get_dflt_eventq(), ev);
  return 0;
}

