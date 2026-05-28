#include <esp_log.h>
#include <stddef.h>
#include <string.h>
#include <host/ble_att.h>
#include <host/ble_hs.h>

#include "common.h"
#include "ble/gap.h"
#include "ble/gatt.h"
#include "ml/service.h"
#include "ble/client_buffer.h"

static const char tag[] = APP_TAG "-ml-service";

struct ble_client_buffer ml_model_buffer = BLE_CLIENT_BUFFER_INIT;

#define ML_RESULTS_MAX_PAYLOAD 256
static uint8_t ml_results_payload[ML_RESULTS_MAX_PAYLOAD];
static uint16_t ml_results_len = 0;
static uint8_t ml_last_error = 0;

void ml_send_snapshot_start(uint32_t start_ms, uint32_t end_ms) {
  uint8_t payload[9];
  payload[0] = 0;
  memcpy(payload + 1, &start_ms, sizeof(start_ms));
  memcpy(payload + 5, &end_ms, sizeof(end_ms));
  ble_gatt_notify_chr(ml_results_chr_handle, payload, sizeof(payload));
}

void ml_send_results(const int8_t *inputs, const int8_t *outputs, size_t count) {
  uint16_t conn = ble_gap_get_conn_handle();
  uint16_t mtu = BLE_ATT_MTU_DFLT;
  if (conn != BLE_HS_CONN_HANDLE_NONE) {
    uint16_t m = ble_att_mtu(conn);
    if (m != 0) mtu = m;
  }

  // -3 ATT overhead, -1 type byte
  uint16_t max_values = (uint16_t)(mtu - 4);
  if (max_values % 2 != 0) max_values--;
  size_t max_pairs = max_values / 2;
  if (max_pairs == 0) return;

  size_t offset = 0;
  while (offset < count) {
    size_t pairs = count - offset;
    if (pairs > max_pairs) pairs = max_pairs;

    if (1 + pairs * 2 > ML_RESULTS_MAX_PAYLOAD) {
      pairs = (ML_RESULTS_MAX_PAYLOAD - 1) / 2;
    }

    ml_results_payload[0] = 1;
    memcpy(ml_results_payload + 1, inputs + offset, pairs);
    memcpy(ml_results_payload + 1 + pairs, outputs + offset, pairs);
    ml_results_len = (uint16_t)(1 + pairs * 2);

    ble_gatt_notify_chr(ml_results_chr_handle, ml_results_payload, ml_results_len);

    offset += pairs;
  }
}

void ml_report_error(enum ml_error_code code) {
  ml_last_error = (uint8_t)code;
  ble_gatt_notify_chr(ml_errors_chr_handle, &ml_last_error, sizeof(ml_last_error));
}

int ml_errors_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)attr_handle;
  (void)arg;

  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR: 
      if (os_mbuf_append(ctxt->om, &ml_last_error, sizeof(ml_last_error)) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
      }
      break;

    default:
      ESP_LOGE(tag, "illegal operation to HR chr with code: %d", ctxt->op);
      return BLE_ATT_ERR_UNLIKELY;
  }

  return 0;
}

