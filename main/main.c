/* STD APIs */
#include <stdbool.h>
#include <stdint.h>

/* ESP APIs */
#include <esp_log.h>
#include <nvs_flash.h>
#include <host/ble_hs_adv.h>
#include <sdkconfig.h>

/* FreeRTOS APIs */
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/* NimBLE stack APIs */
#include <host/ble_hs.h>
#include <host/ble_uuid.h>
#include <host/util/util.h>
#include <nimble/ble.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <host/ble_gap.h>
#include <services/gap/ble_svc_gap.h>

static const char *tag = "Example_NimBLE_ESP32";

static const uint16_t adv_interval_ms = 500;
static const uint16_t adv_interval_max_ms = 510;

static const uint8_t ble_addr_type = BLE_ADDR_RANDOM;

static const char device_name[] = "Heart rate monitor";
static const char device_name_short[] = "HRM";

static const uint16_t gap_appearance_heart = 0x0340;
static const ble_uuid16_t heart_rate_svc_uuid = BLE_UUID16_INIT(0x180D);

static const ble_uuid16_t adv_svc_uuids[] = {heart_rate_svc_uuid};

int prepare_adv() {
  // Get address
  ble_addr_t ble_addr = {.type = ble_addr_type, .val = {0}};

  int is_nrpa;
  int err = ble_hs_id_copy_addr(ble_addr.type, ble_addr.val, &is_nrpa);
  if (err != 0 || is_nrpa) {
    err = ble_hs_id_gen_rnd(false, &ble_addr);
    if (err != 0) {
      ESP_LOGE(tag, "failed to generate random address with error code %d",
               err);
      return 1;
    }

    err = ble_hs_id_set_rnd(ble_addr.val);
    if (err != 0) {
      ESP_LOGE(tag, "failed to set random address with error code %d", err);
      return 1;
    }
  }

  const struct ble_hs_adv_fields adv_fields = {
    .flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,

    .uuids16 = adv_svc_uuids,
    .num_uuids16 = sizeof(adv_svc_uuids) / sizeof(adv_svc_uuids[0]),
    .uuids16_is_complete = 0,

    .name = (const uint8_t *)device_name_short,
    .name_len = strlen(device_name_short),
    .name_is_complete = 0,

    .tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO,
    .tx_pwr_lvl_is_present = 1,

    .appearance = gap_appearance_heart,  // Heart
    .appearance_is_present = 1,

    .le_role = 0,  // Preripheral
    .le_role_is_present = 1,

    .adv_itvl = BLE_GAP_ADV_ITVL_MS(adv_interval_ms),
    .adv_itvl_is_present = 1,
  };

  err = ble_gap_adv_set_fields(&adv_fields);
  if (err != 0) {
    ESP_LOGE(tag, "failed to set advertising data with error code %d", err);
    return 1;
  }

  const struct ble_hs_adv_fields rsp_fields = {
    .name = (const uint8_t *)device_name,
    .name_len = strlen(device_name),
    .name_is_complete = 1,

    .device_addr = ble_addr.val,
    .device_addr_type = ble_addr.type,
    .device_addr_is_present = 1,
  };
  err = ble_gap_adv_rsp_set_fields(&rsp_fields);
  if (err != 0) {
    ESP_LOGE(tag, "failed to set scan response data with error code %d", err);
    return 1;
  }

  return 0;
}

int start_adv();

static int gap_event_handler(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        ESP_LOGI(tag, "connection established with handle %d",
                 event->connect.conn_handle);
      } else {
        ESP_LOGI(tag, "connection failed with status %d",
                 event->connect.status);
      }
      break;

    case BLE_GAP_EVENT_DISCONNECT:
      ESP_LOGI(tag, "connection finished with handle %d and reason %d",
               event->disconnect.conn.conn_handle, event->disconnect.reason);
      start_adv();
      break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
      if (event->adv_complete.reason != 0) {
        ESP_LOGI(tag, "advertising stopped with reason %d",
                 event->adv_complete.reason);
      } else {
        ESP_LOGI(tag, "advertising stopped");
      }
      break;

    case BLE_GAP_EVENT_SUBSCRIBE:
      ESP_LOGI(tag, "client sub");
      if (event->subscribe.cur_notify) {
        if (!event->subscribe.prev_notify) {
          ESP_LOGI(tag, "connection %d enabled notifications for %d",
                   event->subscribe.conn_handle, event->subscribe.attr_handle);
          // GATT notification subscribe callback
        }

      } else if (event->subscribe.prev_notify) {
        ESP_LOGI(tag, "connection %d disabled notifications for %d",
                 event->subscribe.conn_handle, event->subscribe.attr_handle);
        // GATT indication desubscribe callback
      }

      if (event->subscribe.cur_indicate) {
        if (!event->subscribe.prev_indicate) {
          ESP_LOGI(tag, "connection %d enabled indications for %d",
                   event->subscribe.conn_handle, event->subscribe.attr_handle);
          // GATT indication subscribe callback
        }

      } else if (event->subscribe.prev_indicate) {
        ESP_LOGI(tag, "connection %d disabled indications for %d",
                 event->subscribe.conn_handle, event->subscribe.attr_handle);
        // GATT indication desubscribe callback
      }
      break;

    default:
      break;
  }

  return 0;
}

int start_adv() {
  if (ble_gap_adv_active() != 0) {
    return 0;
  }

  struct ble_gap_adv_params adv_params = {0};
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

  adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(adv_interval_ms);
  adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(adv_interval_max_ms);

  int err = ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                              gap_event_handler, NULL);
  if (err != 0) {
    ESP_LOGE(tag, "failed to start advertising with error code %d", err);
    return 1;
  }

  ESP_LOGI(tag, "advertising started");
  return 0;
}

int stop_adv(void) {
  if (ble_gap_adv_active() == 0) {
    return 0;
  }

  int err = ble_gap_adv_stop();
  if (err != 0) {
    ESP_LOGI(tag, "failed to stop advertising with error code %d", err);
    return 1;
  }

  ESP_LOGI(tag, "advertising stopped");
  return 0;
}

static void on_stack_reset(int reason) {
  ESP_LOGI(tag, "nimble stack reset with reason %d", reason);
}

static void on_stack_sync(void) {
  prepare_adv();
  start_adv();
}

static void nimble_host_task(void *param) {
  ESP_LOGI(tag, "starting NimBLE port task");
  ble_hs_cfg.reset_cb = on_stack_reset;
  ble_hs_cfg.sync_cb = on_stack_sync;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

  nimble_port_run();
  vTaskDelete(NULL);
}

void app_main(void) {
  int err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  if (err != ESP_OK) {
    ESP_LOGE(tag, "failed to initialize nvs flash with error code %d ", err);
    return;
  }

  // NimBLE stach initialization
  err = nimble_port_init();
  if (err != ESP_OK) {
    ESP_LOGE(tag, "failed to initialize nimble stack with error code %d ", err);
    return;
  }

  // GAP service initialization
  ble_svc_gap_init();
  err = ble_svc_gap_device_name_set(device_name);
  if (err != 0) {
    ESP_LOGE(tag, "failed to set device name with error code %d", err);
    return;
  }
  err = ble_svc_gap_device_appearance_set(gap_appearance_heart);
  if (err != 0) {
    ESP_LOGE(tag, "failed to set device appearance with error code %d", err);
    return;
  }

  xTaskCreate(nimble_host_task, "NimBLE Host", 4 * 1024, NULL, 5, NULL);
}
