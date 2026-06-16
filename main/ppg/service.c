#include <string.h>

#include <host/ble_hs.h>
#include <host/ble_att.h>

#include "common.h"
#include "ble/gap.h"
#include "ble/gatt.h"
#include "ble/host.h"
#include "ppg/sensor.h"
#include "ppg/service.h"
#include "utils/packet_builder.h"

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

  const struct packet_segment segments[] = {
    { (const uint8_t *)ppg, ppg_count * sizeof(float) },
    { (const uint8_t *)acc, acc_count * sizeof(float) },
  };
  const uint32_t data_total = segments[0].len + segments[1].len;

  // First packet: window-start packets carry type byte 1 + slice seconds +
  // sequence number, otherwise just the continuation type byte 0.
  uint8_t hdr[6];
  uint8_t hdr_len;
  if (window_start) {
    hdr[0] = 1u;
    hdr[1] = PPG_SLICE_SECONDS;
    memcpy(hdr + 2, &sequence_n, sizeof(sequence_n));
    hdr_len = 6u;
  } else {
    hdr[0] = 0u;
    hdr_len = 1u;
  }

  uint32_t sent = 0;
  if (!packet_builder_send(conn, ppg_data_chr_handle, hdr, hdr_len,
      segments, 2, max_payload, &sent)) {
    return;
  }

  // Remaining packets: a single continuation type byte 0.
  uint8_t cont_hdr = 0u;
  while (sent < data_total) {
    if (!packet_builder_send(conn, ppg_data_chr_handle, &cont_hdr, sizeof(cont_hdr),
        segments, 2, max_payload, &sent)) {
      return;
    }
  }
}
