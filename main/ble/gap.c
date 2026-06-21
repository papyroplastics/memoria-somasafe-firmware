#include <sdkconfig.h>
#include <stdint.h>
#include <stdbool.h>
#include <esp_log.h>
#include <esp_random.h>

#include <nimble/ble.h>
#include <nimble/hci_common.h>
#include <host/ble_gap.h>
#include <host/util/util.h>
#include <services/gap/ble_svc_gap.h>
#include <os/os_mbuf.h>

#include "common.h"
#include "ble/gap.h"
#include "ble/gatt.h"
#include "ble/host.h"
#include "ppg/service.h"
#include "ml/service.h"

static const char tag[] = APP_TAG "-gap";

static const uint16_t device_appearance = 0x0340;  // heart

static const uint8_t adv_instance = 0;
static const uint16_t adv_interval_ms = 500;
static const uint16_t adv_interval_max_ms = 510;
static const uint8_t ble_addr_type = BLE_OWN_ADDR_PUBLIC;

static uint16_t cur_conn_handle = 0;

uint16_t ble_gap_get_conn_handle(void) {
  return cur_conn_handle;
}

bool ble_gap_check_conn_encrypted(uint16_t conn_handle) {
    struct ble_gap_conn_desc desc;
    int err = ble_gap_conn_find(conn_handle, &desc);
    if (err != 0) return false;

    if (!desc.sec_state.encrypted) {
        ble_gap_security_initiate(conn_handle);
        return false;
    }
    
    return true;
}

int ble_gap_task_prepare(void) {
  ble_svc_gap_init();

  return ble_svc_gap_device_name_set(device_name) != 0
    || ble_svc_gap_device_appearance_set(device_appearance) != 0;
}

static int connection_event_handler(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT:
    if (event->connect.status == 0) {
      ESP_LOGI(tag, "connection established with client %d",
          event->connect.conn_handle);

      cur_conn_handle = event->connect.conn_handle;

    } else {
      ESP_LOGI(tag, "connection failed with status %d", event->connect.status);
      ble_gap_advert_start();
    }
    break;

  case BLE_GAP_EVENT_DISCONNECT:
    ESP_LOGI(tag, "connection finished with client %d and reason %d",
             event->disconnect.conn.conn_handle, event->disconnect.reason);

    cur_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    ble_gap_advert_start();
    break;

  case BLE_GAP_EVENT_ADV_COMPLETE:
    ESP_LOGI(tag, "advertising stopped with reason %d %s",
        event->adv_complete.reason, 
        event->adv_complete.reason == 0 ? "(connection)" : ""
    );
    break;

  case BLE_GAP_EVENT_SUBSCRIBE:
    ESP_LOGI(tag, 
        "client %d updated subscription status on attrubute %d:\n"
        "\tnotify: %d -> %d\n\tindicate: %d -> %d",
        event->subscribe.conn_handle, event->subscribe.attr_handle,
        event->subscribe.prev_notify, event->subscribe.cur_notify, 
        event->subscribe.prev_indicate, event->subscribe.cur_indicate
    );
    ble_gatt_subscribe_cb(event->subscribe.attr_handle, event->subscribe.cur_notify);

#if SMP_SECURITY_LEVEL != 0
    if (!ble_gap_check_conn_encrypted(event->subscribe.conn_handle)) {
      ESP_LOGE(tag, "subscribe event for attribute %d on unencrypted connection %d", 
          event->subscribe.attr_handle, event->subscribe.conn_handle);
    }
#endif

    break;

  case BLE_GAP_EVENT_MTU:
    ESP_LOGI(tag, "mtu updated to %d", event->mtu.value);
    break;

  case BLE_GAP_EVENT_ENC_CHANGE:
    if (event->enc_change.status == 0) {
      ESP_LOGI(tag, "connection encryption activated");
    } else {
      ESP_LOGE(tag, "connection encryption failed with error code %d", event->enc_change.status);
    }
    break;

  case BLE_GAP_EVENT_REPEAT_PAIRING: {
    struct ble_gap_conn_desc desc;
    int err = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
    if (err != 0) {
      ESP_LOGE(tag, "connection not found for repeat pairing event, error code %d", err);
      return BLE_GAP_REPEAT_PAIRING_IGNORE;
    }

    ble_store_util_delete_peer(&desc.peer_id_addr);
    ESP_LOGI(tag, "repairing procedure initialized");

    return BLE_GAP_REPEAT_PAIRING_RETRY;
  }

  case BLE_GAP_EVENT_PASSKEY_ACTION: {
    struct ble_sm_io pkey = {0};
    pkey.action = event->passkey.params.action;

    switch (event->passkey.params.action) {
      case BLE_SM_IOACT_DISP: 
        pkey.passkey = 100000 + esp_random() % 900000;
        ESP_LOGI(tag, "enter passkey %" PRIu32 " on the peer side", pkey.passkey);

        int err = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        if (err != 0) {
          ESP_LOGE(tag, "failed to inject security manager io, error code: %d", err);
          return err;
        }
        break;

      default:
        ESP_LOGE(tag, "unsupported passkey action requested %d", event->passkey.params.action);
    }
    break;
  }

  default:
    break;
  }

  return 0;
}

static int get_random_address(ble_addr_t *ble_addr) {
  int is_nrpa;
  int err = ble_hs_id_copy_addr(BLE_OWN_ADDR_RANDOM, ble_addr->val, &is_nrpa);
  if (err == 0 && !is_nrpa) {
    ble_addr->type = BLE_OWN_ADDR_RANDOM;
    return 0;
  }

  err = ble_hs_id_gen_rnd(0, ble_addr);
  if (err != 0) {
    ESP_LOGE(tag, "failed to generate random address with error code %d", err);
    return 1;
  }

  err = ble_hs_id_set_rnd(ble_addr->val);
  if (err != 0) {
    ESP_LOGE(tag, "failed to set random address with error code %d", err);
    return 1;
  }

  return 0;
}

static int get_public_address(ble_addr_t *ble_addr) {
  int is_nrpa;
  int err = ble_hs_id_copy_addr(BLE_OWN_ADDR_PUBLIC, ble_addr->val, &is_nrpa);
  if (err != 0 || is_nrpa) {
    return 1;
  }

  ble_addr->type = BLE_OWN_ADDR_PUBLIC;
  return 0;
}

static int get_address(ble_addr_t *ble_addr, uint8_t prefered_addr_type) {
  int err;
  switch (prefered_addr_type) {
    case BLE_OWN_ADDR_PUBLIC:
      err = get_public_address(ble_addr);
      break;
    case BLE_OWN_ADDR_RANDOM:
      err = get_random_address(ble_addr);
      break;
    default:
      ESP_LOGE(tag, "invalid address type requested %d", prefered_addr_type);
      return 1;
  }

  if (err != 0) {
    ESP_LOGE(tag, "unable to set device address");
    return 1;
  }

  ESP_LOGI(tag, "using %s address " MAC_ADDR_STR, 
      MAC_ADDR_TYPE(ble_addr->type), MAC_ADDR_ITEM(ble_addr->val)
  );

  return 0;
}

void set_adv_fields(struct ble_hs_adv_fields *fields, int8_t tx_power, ble_addr_t *ble_addr) {
  fields->flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

  fields->name = (const uint8_t *)device_short_name;
  fields->name_len = device_short_name_len;
  fields->name_is_complete = 0;

  fields->tx_pwr_lvl = tx_power;
  fields->tx_pwr_lvl_is_present = 1;

  fields->adv_itvl = BLE_GAP_ADV_ITVL_MS(adv_interval_ms);
  fields->adv_itvl_is_present = 1;

  fields->device_addr = ble_addr->val;
  fields->device_addr_type = ble_addr->type;
  fields->device_addr_is_present = 1;
}

void set_rsp_fields(struct ble_hs_adv_fields *fields) {
  fields->name = (const uint8_t *)device_name;
  fields->name_len = device_name_len;
  fields->name_is_complete = 1;

  // Heart
  fields->appearance = device_appearance;
  fields->appearance_is_present = 1;

  // Preripheral
  fields->le_role = 0;
  fields->le_role_is_present = 1;
}

#ifdef CONFIG_BT_NIMBLE_EXT_ADV

struct os_mbuf *get_adv_fields_mbuf(const struct ble_hs_adv_fields *adv_fields) {
  struct os_mbuf *adv_data_buf = os_msys_get_pkthdr(BLE_HCI_MAX_EXT_ADV_DATA_LEN, 0);
  if (adv_data_buf == NULL) {
    ESP_LOGE(tag, "failed to allocate mbuf for adv data");
    return NULL;
  }

  int err = ble_hs_adv_set_fields_mbuf(adv_fields, adv_data_buf);
  if (err != 0) {
    ESP_LOGE(tag, "failed to write adv fields into mbuf with error code %d", err);
    return NULL;
  }
  
  return adv_data_buf;
}

int ble_gap_advert_config(void) {
  ble_addr_t ble_addr = {0};

  int err = get_address(&ble_addr, ble_addr_type);
  if (err != 0) {
    return 1;
  }

  struct ble_gap_ext_adv_params params = {
    .connectable = 1,

#if EXT_ADV_LEGACY_PDU
    .scannable = 1,
    .legacy_pdu = 1,
#else 
    .include_tx_power = 1,
#endif

    .itvl_min = BLE_GAP_ADV_ITVL_MS(adv_interval_ms),
    .itvl_max = BLE_GAP_ADV_ITVL_MS(adv_interval_max_ms),
    
    .own_addr_type = ble_addr.type,

    .primary_phy = BLE_HCI_LE_PHY_1M,
    .secondary_phy = BLE_HCI_LE_PHY_2M,

    .tx_power = 127,
    .sid = 1,
  };

  int8_t tx_power;
  err = ble_gap_ext_adv_configure(adv_instance, &params, &tx_power, connection_event_handler, NULL);
  if (err != 0) {
    ESP_LOGE(tag, "unable to configure BLE extended advertisement with error code %d", err);
  }

  if (ble_addr.type == BLE_OWN_ADDR_RANDOM) {
    err = ble_gap_ext_adv_set_addr(adv_instance, &ble_addr);
    if (err != 0) {
      ESP_LOGE(tag, "unable to set address for BLE extended advertisement with error code %d", err);
    }
  }

  // Advertisement data
  struct ble_hs_adv_fields adv_fields = {0};
  set_adv_fields(&adv_fields, tx_power, &ble_addr);

#if !EXT_ADV_LEGACY_PDU
  adv_fields.uuids128 = adv_svc_uuid128;
  adv_fields.num_uuids128 = adv_svc_uuid128_cnt;
  adv_fields.uuids128_is_complete = 1;

  set_rsp_fields(&adv_fields);
#endif

  struct os_mbuf *adv_data_buf = get_adv_fields_mbuf(&adv_fields);
  if (adv_data_buf == NULL) return 1;

  err = ble_gap_ext_adv_set_data(adv_instance, adv_data_buf);
  if (err != 0) {
    ESP_LOGE(tag, "failed configure adv data for extended adv with error code %d", err);
    return 1;
  }
  
#if EXT_ADV_LEGACY_PDU
  struct ble_hs_adv_fields rsp_fields = {0};
  set_rsp_fields(&rsp_fields);

  adv_data_buf = get_adv_fields_mbuf(&rsp_fields);
  if (adv_data_buf == NULL) return 1;

  err = ble_gap_ext_adv_rsp_set_data(adv_instance, adv_data_buf);
  if (err != 0) {
    ESP_LOGE(tag, "failed configure rsp data for extended adv with error code %d", err);
    return 1;
  }
#endif

#if CONFIG_BT_NIMBLE_ENABLE_PERIODIC_ADV
  // Periodic advertising
  struct ble_gap_periodic_adv_params pparams = {
    .include_tx_power = 1,
    .itvl_min = BLE_GAP_ADV_ITVL_MS(adv_interval_ms),
    .itvl_max = BLE_GAP_ADV_ITVL_MS(adv_interval_max_ms),

  };

  err = ble_gap_periodic_adv_configure(adv_instance, &pparams);
  if (err != 0) {
    ESP_LOGE(tag, "failed to configure periodic advertising");
    return 1;
  }

  adv_data_buf = os_msys_get_pkthdr(device_name_len, 0);
  if (adv_data_buf == NULL) {
    ESP_LOGE(tag, "failed to allocate mbuf for periodic adv");
    return 1;
  }

  err = os_mbuf_append(adv_data_buf, device_name, device_name_len);
  if (adv_data_buf == NULL) {
    ESP_LOGE(tag, "failed to write data for periodic adv");
    return 1;
  }

  err = ble_gap_periodic_adv_set_data(adv_instance, adv_data_buf);
  if (adv_data_buf == NULL) {
    ESP_LOGE(tag, "failed to set data for periodic adv");
    return 1;
  }

  err = ble_gap_periodic_adv_start(adv_instance);
  if (adv_data_buf == NULL) {
    ESP_LOGE(tag, "failed to start periodic adv");
    return 1;
  }
#endif

  return 0;
}

int ble_gap_advert_start(void) {
  if (ble_gap_ext_adv_active(adv_instance) != 0) {
    return 0;
  }

  int err = ble_gap_ext_adv_start(adv_instance, 0, 0);
  if (err != 0) {
    ESP_LOGE(tag, "failed to start extended advertising with error code %d", err);
    return 1;
  }

  ESP_LOGI(tag, "extended advertising started");
  return 0;
}

int ble_gap_advert_stop(void) {
  if (ble_gap_ext_adv_active(adv_instance) == 0) {
    return 0;
  }

  int err = ble_gap_ext_adv_stop(adv_instance);
  if (err != 0) {
    ESP_LOGI(tag, "failed to stop extended advertising with error code %d", err);
    return 1;
  }

  ESP_LOGI(tag, "extended advertising stopped by application");
  return 0;
}

#else // CONFIG_BT_NIMBLE_EXT_ADV

int ble_gap_legacy_advert_config(void) {
  ble_addr_t ble_addr = {0};
  int err = get_address(&ble_addr, ble_addr_type);
  if (err != 0) {
    return 1;
  }

  // Set advertisement data
  struct ble_hs_adv_fields adv_fields = {0};
  set_adv_fields(&adv_fields, BLE_HS_ADV_TX_PWR_LVL_AUTO, &ble_addr);

  err = ble_gap_adv_set_fields(&adv_fields);
  if (err != 0) {
    ESP_LOGE(tag, "failed to set advertising data with error code %d", err);
    return 1;
  }

  struct ble_hs_adv_fields rsp_fields = {0};
  set_rsp_fields(&rsp_fields);

  err = ble_gap_adv_rsp_set_fields(&rsp_fields);
  if (err != 0) {
    ESP_LOGE(tag, "failed to set scan response data with error code %d", err);
    return 1;
  }

  return 0;
}

int ble_gap_legacy_advert_start() {
  if (ble_gap_adv_active() != 0) {
    return 0;
  }

  struct ble_gap_adv_params adv_params = {
    .conn_mode = BLE_GAP_CONN_MODE_UND,
    .disc_mode = BLE_GAP_DISC_MODE_GEN,
    
    .itvl_min = BLE_GAP_ADV_ITVL_MS(adv_interval_ms),
    .itvl_max = BLE_GAP_ADV_ITVL_MS(adv_interval_max_ms),
  };

  int err = ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                              connection_event_handler, NULL);
  if (err != 0) {
    ESP_LOGE(tag, "failed to start advertising with error code %d", err);
    return 1;
  }

  ESP_LOGI(tag, "advertising started");
  return 0;
}

int ble_gap_legacy_advert_stop(void) {
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
#endif // CONFIG_BT_NIMBLE_EXT_ADV
