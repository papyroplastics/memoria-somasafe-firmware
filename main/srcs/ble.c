#include "common.h"
#include "ble.h"
#include "gap.h"
#include "gatt.h"

#include <nimble/nimble_port.h>
#include <nimble/nimble_npl_os.h>
#include <stdlib.h>

static const char tag[] = "nimble-example-ble";

const char device_name[] = "Heart rate monitor";
const char device_name_short[] = "HRM";
const uint16_t device_appearance = 0x0340;  // heart

const ble_uuid16_t hr_svc_uuid = BLE_UUID16_INIT(0x180D);
const ble_uuid16_t hr_chr_uuid = BLE_UUID16_INIT(0x2A37);

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

