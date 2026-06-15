#include <string.h>

#include <esp_log.h>
#include <host/ble_hs.h>
#include <host/ble_att.h>
#include <os/os_mbuf.h>

#include "common.h"
#include "ble/gap.h"
#include "ble/gatt.h"
#include "ble/host.h"
#include "ppg/sensor.h"
#include "ppg/service.h"

static const char tag[] = APP_TAG "-ppg-service";

void ppg_data_notify_send(const float *ppg, uint16_t ppg_count,
                          const float *acc, uint16_t acc_count,
                          bool window_start, uint32_t sequence_n) {

  if (!ppg_data_chr_notify) return;

  uint16_t conn = ble_gap_get_conn_handle();
  if (conn == BLE_HS_CONN_HANDLE_NONE) return;

  ASSERT_ENCRYPYED()

  uint16_t mtu = ble_att_mtu(conn);
  if (mtu < BLE_ATT_MTU_DFLT) return;

  uint16_t max_payload = mtu - 3;

  const uint32_t ppg_bytes  = ppg_count * sizeof(float);
  const uint32_t acc_bytes  = acc_count * sizeof(float);
  const uint32_t data_total = ppg_bytes + acc_bytes;

  bool is_start = window_start;
  uint32_t sent = 0;

  while (sent < data_total) {
    // Header: type byte; start packets also carry slice seconds + sequence_n
    uint8_t hdr[6];
    uint8_t hdr_len;
    if (is_start) {
      hdr[0] = 1u;
      hdr[1] = PPG_SLICE_SECONDS;
      memcpy(hdr + 2, &sequence_n, sizeof(sequence_n));
      hdr_len = 6u;
      is_start = false;
    } else {
      hdr[0] = 0u;
      hdr_len = 1u;
    }

    uint16_t body_capacity = (max_payload - hdr_len) / sizeof(float) * sizeof(float);
    uint16_t body_len = data_total - sent;
    if (body_len > body_capacity) body_len = body_capacity;

    struct os_mbuf *om = os_msys_get_pkthdr(hdr_len + body_len, 0);
    if (om == NULL) {
      ESP_LOGE(tag, "failed to allocate notify mbuf for ppg data notification");
      return;
    }

    if (os_mbuf_append(om, hdr, hdr_len) != 0) {
      os_mbuf_free_chain(om);
      return;
    }

    uint32_t remain = body_len;
    if (sent < ppg_bytes) {
      uint32_t n = ppg_bytes - sent;
      if (n > remain) n = remain;
      remain -= n;

      if (os_mbuf_append(om, (const uint8_t *)ppg + sent, n) != 0) {
        os_mbuf_free_chain(om);
        return;
      }
    }

    if (remain > 0 && sent >= ppg_bytes) {
      uint32_t acc_offset = sent - ppg_bytes;
      if (os_mbuf_append(om, (const uint8_t *)acc + acc_offset, remain) != 0) {
        os_mbuf_free_chain(om);
        return;
      }
    }

    int err = ble_gatts_notify_custom(conn, ppg_data_chr_handle, om);
    if (err != 0) {
      ESP_LOGW(tag, "ppg data notification failed with reason %d", err);
    }

    sent += body_len;
  }
}
