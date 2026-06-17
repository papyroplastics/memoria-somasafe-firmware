#include <string.h>

#include <host/ble_hs.h>
#include <host/ble_att.h>

#include "common.h"
#include "ble/gap.h"
#include "ble/gatt.h"
#include "ble/host.h"
#include "ppg/sensor.h"
#include "ppg/service.h"
#include "utils/notif_transaction.h"

void ppg_data_notify_send(struct transaction_state *tx,
                          const float *ppg, uint16_t ppg_count,
                          const float *acc, uint16_t acc_count,
                          bool window_start, bool window_end,
                          uint32_t sequence_n, uint32_t start_ms, uint32_t end_ms) {

  if (!ppg_data_chr_notify) return;

  uint16_t conn = ble_gap_get_conn_handle();
  if (conn == BLE_HS_CONN_HANDLE_NONE) return;

  ASSERT_ENCRYPYED()

  uint16_t mtu = ble_att_mtu(conn);
  if (mtu < BLE_ATT_MTU_DFLT) return;

  uint16_t max_payload = mtu - 3;

  // Service framing: a header on the first call (slice duration unit + sequence
  // number + on-device start timestamp) and a tail on the last call (on-device
  // end timestamp). Timestamps are device-uptime milliseconds.
  uint8_t svc_hdr[1 + sizeof(sequence_n) + sizeof(start_ms)];
  svc_hdr[0] = PPG_SLICE_SECONDS;
  memcpy(svc_hdr + 1, &sequence_n, sizeof(sequence_n));
  memcpy(svc_hdr + 1 + sizeof(sequence_n), &start_ms, sizeof(start_ms));

  uint8_t svc_tail[sizeof(end_ms)];
  memcpy(svc_tail, &end_ms, sizeof(end_ms));

  struct packet_segment segments[4];
  size_t n = 0;
  if (window_start) segments[n++] = (struct packet_segment){ svc_hdr, sizeof(svc_hdr) };
  segments[n++] = (struct packet_segment){ (const uint8_t *)ppg, ppg_count * sizeof(float) };
  segments[n++] = (struct packet_segment){ (const uint8_t *)acc, acc_count * sizeof(float) };
  if (window_end) segments[n++] = (struct packet_segment){ svc_tail, sizeof(svc_tail) };

  notif_transaction_send(tx, conn, ppg_data_chr_handle,
      segments, n, max_payload, window_start, window_end);
}
