#include <stdint.h>
#include <esp_log.h>
#include <host/ble_hs.h>
#include <nimble/nimble_port.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "common.h"
#include "ble/host.h"
#include "ble/gatt.h"
#include "ble/gap.h"
#include "nimble/nimble_port_freertos.h"

#include <store/config/ble_store_config.h>

static const char tag[] = APP_TAG "-ble";

const char device_name[] = "SomaSafe Device";
const uint8_t device_name_len = strlen(device_name);
const char device_short_name[] = "PPG";
const uint8_t device_short_name_len = strlen(device_short_name);

static void on_stack_reset(int reason) {
  ESP_LOGI(tag, "NimBLE stack reset with reason %d", reason);
}

static void on_stack_sync(void) {
  ble_gap_advert_config();
  ble_gap_advert_start();
}

int ble_init() {
  int err = nimble_port_init();
  if (err != ESP_OK) {
    ESP_LOGE(tag, "failed to initialize NimBLE stack");
    return 1;
  }

  ble_hs_cfg.sync_cb = on_stack_sync;
  ble_hs_cfg.reset_cb = on_stack_reset;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
  //ble_hs_cfg.gatts_register_cb = ble_gatt_register_cb;

#if SMP_SECURITY_LEVEL > 0
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_sc_only = 0;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_sec_lvl = SMP_SECURITY_LEVEL;

  ble_hs_cfg.sm_our_key_dist   |= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

#if SMP_SECURITY_LEVEL > 1
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
  ble_hs_cfg.sm_mitm = 1;
#endif

#endif

  // NimBLE interface not exported on a header for some reason
  void ble_store_config_init(void);
  ble_store_config_init();

  err = ble_gap_task_prepare();
  if (err != 0) {
    ESP_LOGE(tag, "failed to initialize GAP service");
    return 1;
  }

  err = ble_gatt_task_prepare();
  if (err != 0) {
    ESP_LOGE(tag, "failed to initialize GATT service");
    return 1;
  }

  return 0;
}

void ble_task(void *param) {
  (void)param;

  ESP_LOGI(tag, "starting NimBLE stack");
  nimble_port_run();

  nimble_port_freertos_deinit();

  ESP_LOGE(tag, "nimble port task exited unexpectedly");
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
int ble_work_queue_push_task(void (*cb)(void*), void *arg) {
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
