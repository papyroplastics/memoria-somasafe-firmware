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
#include "ble/host.h"
#include "ml/service.h"
#include "ble/client_buffer.h"

static const char tag[] = APP_TAG "-ml-service";

struct ble_client_buffer ml_model_buffer = BLE_CLIENT_BUFFER_INIT("model");

void ml_result_notify_send(uint32_t sequence_n,
                           const int8_t *features, size_t features_len,
                           const int8_t *result, size_t result_len) {
  if (!ml_result_chr_notify) return;

  uint16_t conn = ble_gap_get_conn_handle();
  if (conn == BLE_HS_CONN_HANDLE_NONE) return;

  ASSERT_ENCRYPYED()

  uint16_t mtu = ble_att_mtu(conn);
  if (mtu < BLE_ATT_MTU_DFLT) return;

  uint16_t max_payload = mtu - 3;

  const size_t data_total = features_len + result_len;

  bool is_start = true;
  size_t sent = 0;

  while (sent < data_total) {
    uint8_t hdr[5];
    uint8_t hdr_len;

    if (is_start) {
      hdr[0] = 1;
      memcpy(hdr + 1, &sequence_n, sizeof(sequence_n));
      hdr_len = 5;
      is_start = false;

    } else {
      hdr[0] = 0;
      hdr_len = 1;
    }

    size_t body_capacity = max_payload - hdr_len;
    size_t body_len = data_total - sent;
    if (body_len > body_capacity) body_len = body_capacity;

    struct os_mbuf *om = os_msys_get_pkthdr(hdr_len + body_len, 0);
    if (om == NULL) {
      ESP_LOGE(tag, "failed to allocate notify mbuf");
      return;
    }

    if (os_mbuf_append(om, hdr, hdr_len) != 0) {
      os_mbuf_free_chain(om);
      return;
    }

    uint32_t pos = sent;
    uint32_t remain = body_len;

    if (pos < features_len) {
      uint32_t n = features_len - pos;
      if (n > remain) n = remain;

      if (os_mbuf_append(om, features + pos, n) != 0) {
        os_mbuf_free_chain(om);
        return;
      }

      pos += n;
      remain -= n;
    }

    if (remain > 0) {
      uint32_t result_offset = pos - features_len;
      if (os_mbuf_append(om, result + result_offset, remain) != 0) {
        os_mbuf_free_chain(om);
        return;
      }
    }

    int err = ble_gatts_notify_custom(conn, ml_result_chr_handle, om);
    if (err != 0) {
      ESP_LOGW(tag, "ppg data notification failed with reason %d", err);
    }

    sent += body_len;
  }
}

void ml_error_notify_send(enum ml_error_code code) {
  if (!ml_errors_chr_notify) return;

  uint16_t conn = ble_gap_get_conn_handle();
  if (conn == BLE_HS_CONN_HANDLE_NONE) return;

  ASSERT_ENCRYPYED()

  uint8_t error = (uint8_t)code;
  ble_gatt_notify_chr(ml_errors_chr_handle, &error, sizeof(error));
}

