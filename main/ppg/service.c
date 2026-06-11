#include <string.h>

#include <esp_log.h>
#include <host/ble_hs.h>
#include <host/ble_att.h>
#include <os/os_mbuf.h>

#include "common.h"
#include "ble/gap.h"
#include "ble/gatt.h"
#include "ppg/sensor.h"
#include "ppg/service.h"

static const char tag[] = APP_TAG "-ppg-service";

void ppg_notify_data(const float *ppg, uint16_t ppg_count,
                     const float *acc, uint16_t acc_count,
                     bool window_start, uint32_t sequence_n) {
  uint16_t conn = ble_gap_get_conn_handle();
  if (conn == BLE_HS_CONN_HANDLE_NONE) return;

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
    } else {
      hdr[0] = 0u;
      hdr_len = 1u;
    }

    uint16_t space = max_payload - hdr_len;
    space = (space / sizeof(float)) * sizeof(float);  // align to float boundary

    uint16_t chunk = (uint16_t)((data_total - sent < space)
                                  ? (data_total - sent) : space);

    struct os_mbuf *om = os_msys_get_pkthdr(hdr_len + chunk, 0);
    if (om == NULL) {
      ESP_LOGE(tag, "failed to allocate notify mbuf");
      return;
    }

    int err = os_mbuf_append(om, hdr, hdr_len);

    // Append from the logical flat stream [ppg_bytes | acc_bytes]
    uint32_t remain = chunk;
    if (err == 0 && sent < ppg_bytes && remain > 0) {
      uint32_t n = ppg_bytes - sent;
      if (n > remain) n = remain;
      err = os_mbuf_append(om, (const uint8_t *)ppg + sent, n);
      remain -= n;
    }
    if (err == 0 && remain > 0) {
      uint32_t acc_offset = (sent >= ppg_bytes) ? (sent - ppg_bytes) : 0u;
      err = os_mbuf_append(om, (const uint8_t *)acc + acc_offset, remain);
    }

    if (err != 0) {
      ESP_LOGE(tag, "mbuf append failed");
      os_mbuf_free_chain(om);
      return;
    }

    err = ble_gatts_notify_custom(conn, ppg_chr_handle, om);
    if (err != 0) {
      ESP_LOGW(tag, "notify failed: %d", err);
    }

    is_start = false;
    sent += chunk;
  }
}
