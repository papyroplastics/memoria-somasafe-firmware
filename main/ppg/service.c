#include <string.h>

#include <esp_log.h>
#include <host/ble_hs.h>
#include <host/ble_att.h>

#include "common.h"
#include "ble/gap.h"
#include "ble/gatt.h"
#include "ppg/sensor.h"
#include "ppg/service.h"

static const char tag[] = APP_TAG "-ppg-service";

// Max ATT notification payload we allocate on the stack.
// BLE 4.2+ can negotiate MTU up to 517; this covers most real-world cases.
#define PKT_BUF_MAX 512

void ppg_notify_data(const float *ppg, uint16_t ppg_count,
                     const float *acc, uint16_t acc_count,
                     bool window_start) {
  uint16_t conn = ble_gap_get_conn_handle();
  if (conn == BLE_HS_CONN_HANDLE_NONE) return;

  uint16_t mtu = ble_att_mtu(conn);
  if (mtu <= 3) return;

  uint16_t max_payload = mtu - 3;
  if (max_payload > PKT_BUF_MAX) max_payload = PKT_BUF_MAX;

  const uint32_t ppg_bytes  = ppg_count * sizeof(float);
  const uint32_t acc_bytes  = acc_count * sizeof(float);
  const uint32_t data_total = ppg_bytes + acc_bytes;

  bool is_start = window_start;
  uint32_t sent = 0;

  static uint8_t pkt[PKT_BUF_MAX];

  while (sent < data_total) {
    uint8_t  hdr_len = is_start ? 2u : 1u;
    uint16_t space   = max_payload - hdr_len;
    space = (space / sizeof(float)) * sizeof(float);  // align to float boundary
    if (space == 0) {
      ESP_LOGE(tag, "MTU too small for PPG notification");
      return;
    }

    uint16_t chunk = (uint16_t)((data_total - sent < space)
                                  ? (data_total - sent) : space);

    pkt[0] = is_start ? 1u : 0u;
    if (is_start) {
      pkt[1]    = PPG_SLICE_SECONDS;
      is_start  = false;
    }

    // Copy from the logical flat buffer [ppg_bytes | acc_bytes]
    uint8_t *dst   = pkt + hdr_len;
    uint32_t remain = chunk;

    if (sent < ppg_bytes && remain > 0) {
      uint32_t n = ppg_bytes - sent;
      if (n > remain) n = remain;
      memcpy(dst, (const uint8_t *)ppg + sent, n);
      dst    += n;
      remain -= n;
    }
    if (remain > 0) {
      uint32_t acc_offset = (sent >= ppg_bytes) ? (sent - ppg_bytes) : 0u;
      memcpy(dst, (const uint8_t *)acc + acc_offset, remain);
    }

    int err = ble_gatt_notify_chr(ppg_chr_handle, pkt, hdr_len + chunk);
    if (err != 0) {
      ESP_LOGW(tag, "notify failed: %d", err);
    }

    sent += chunk;
  }
}
