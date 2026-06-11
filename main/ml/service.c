#include <esp_log.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <host/ble_att.h>
#include <host/ble_hs.h>
#include <os/os_mbuf.h>

#include "common.h"
#include "ble/gap.h"
#include "ble/gatt.h"
#include "ml/service.h"
#include "ble/client_buffer.h"

static const char tag[] = APP_TAG "-ml-service";

struct ble_client_buffer ml_model_buffer = BLE_CLIENT_BUFFER_INIT;

static uint8_t ml_last_error = 0;

void ml_notify_result(uint32_t sequence_n,
                      const int8_t *features, size_t features_len,
                      const int8_t *score, size_t score_len) {
  uint16_t conn = ble_gap_get_conn_handle();
  if (conn == BLE_HS_CONN_HANDLE_NONE) return;

  uint16_t mtu = ble_att_mtu(conn);
  if (mtu < BLE_ATT_MTU_DFLT) return;

  uint16_t max_payload = mtu - 3;

  const uint8_t *segments[] = {
    (const uint8_t *)features, (const uint8_t *)score,
  };
  const size_t segment_lens[] = { features_len, score_len };
  const size_t data_total = features_len + score_len;

  bool is_start = true;
  size_t sent = 0;

  while (sent < data_total) {
    // Header: type byte; the start packet also carries the sequence number
    uint8_t hdr[5];
    uint8_t hdr_len;
    if (is_start) {
      hdr[0] = 1u;
      memcpy(hdr + 1, &sequence_n, sizeof(sequence_n));
      hdr_len = 5u;
    } else {
      hdr[0] = 0u;
      hdr_len = 1u;
    }

    size_t chunk = data_total - sent;
    if (chunk > (size_t)(max_payload - hdr_len)) chunk = max_payload - hdr_len;

    struct os_mbuf *om = os_msys_get_pkthdr(hdr_len + chunk, 0);
    if (om == NULL) {
      ESP_LOGE(tag, "failed to allocate notify mbuf");
      return;
    }

    int err = os_mbuf_append(om, hdr, hdr_len);

    // Append from the logical flat stream [features | score]
    size_t pos = sent;
    size_t remain = chunk;
    for (size_t s = 0; s < 2 && err == 0 && remain > 0; s++) {
      if (pos >= segment_lens[s]) {
        pos -= segment_lens[s];
        continue;
      }
      size_t n = segment_lens[s] - pos;
      if (n > remain) n = remain;
      err = os_mbuf_append(om, segments[s] + pos, n);
      pos = 0;
      remain -= n;
    }

    if (err != 0) {
      ESP_LOGE(tag, "mbuf append failed");
      os_mbuf_free_chain(om);
      return;
    }

    err = ble_gatts_notify_custom(conn, ml_results_chr_handle, om);
    if (err != 0) {
      ESP_LOGW(tag, "notify failed: %d", err);
    }

    is_start = false;
    sent += chunk;
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

