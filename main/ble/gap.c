#include <host/ble_gap.h>
#include <services/gap/ble_svc_gap.h>

#include "ble/gap.h"
#include "ble/gatt.h"
#include "ble/host.h"


static const char tag[] = "nimble-example-gap";

static const uint16_t adv_interval_ms = 500;
static const uint16_t adv_interval_max_ms = 510;

static const uint8_t ble_addr_type = BLE_ADDR_RANDOM;

static bool cur_conn_active = false;
static uint16_t cur_conn_handle = 0;

static int connection_event_handler(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT:
    if (event->connect.status == 0) {
      ESP_LOGI(tag, "connection established with client %d",
          event->connect.conn_handle);

      cur_conn_active = true;
      cur_conn_handle = event->connect.conn_handle;

    } else {
      ESP_LOGI(tag, "connection failed with status %d", event->connect.status);
    }
    break;

  case BLE_GAP_EVENT_DISCONNECT:
    ESP_LOGI(tag, "connection finished with client %d and reason %d",
             event->disconnect.conn.conn_handle, event->disconnect.reason);

    cur_conn_active = false;
    cur_conn_handle = 0;
    start_adv();
    break;

  case BLE_GAP_EVENT_ADV_COMPLETE:
    ESP_LOGI(tag, "advertising stopped with reason %d %s",
        event->adv_complete.reason, 
        event->adv_complete.reason == 0 ? "(connection)" : ""
    );
    break;

  case BLE_GAP_EVENT_SUBSCRIBE:
    ESP_LOGI(tag, 
        "client %d updated subscription status on attrubute %d:"
        "\n\tnotify: %d -> %d\n\tindicate: $d -> %d",
        event->subscribe.conn_handle, event->subscribe.attr_handle,
        event->subscribe.prev_notify, event->subscribe.cur_notify, 
        event->subscribe.prev_indicate, event->subscribe.cur_indicate
    );
    break;

  default:
    break;
  }

  return 0;
}

int gap_init(void) {
  ble_svc_gap_init();

  return ble_svc_gap_device_name_set(device_name) != 0
    || ble_svc_gap_device_appearance_set(device_appearance) != 0;
}


int prepare_adv(void) {
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

  // Set advertisement data
  const ble_uuid16_t adv_svc_uuids[] = {hr_svc_uuid};

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

    .appearance = device_appearance, // Heart
    .appearance_is_present = 1,

    .le_role = 0, // Preripheral
    .le_role_is_present = 1,

    .adv_itvl = BLE_GAP_ADV_ITVL_MS(adv_interval_ms),
    .adv_itvl_is_present = 1,
  };

  err = ble_gap_adv_set_fields(&adv_fields);
  if (err != 0) {
    ESP_LOGE(tag, "failed to set advertising data with error code %d", err);
    return 1;
  }

  // Set scan response data
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
                              connection_event_handler, NULL);
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

  ESP_LOGI(tag, "advertising stopped by application");
  return 0;
}
