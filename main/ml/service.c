#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <host/ble_att.h>
#include <host/ble_hs.h>

#include "common.h"
#include "ble/gap.h"
#include "ble/gatt.h"
#include "ble/host.h"
#include "ml/service.h"
#include "ble/client_buffer.h"
#include "utils/packet_builder.h"

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

  const struct packet_segment segments[] = {
    { (const uint8_t *)features, features_len },
    { (const uint8_t *)result,   result_len },
  };
  const uint32_t data_total = features_len + result_len;

  // Start packet: type byte 1 followed by the slice sequence number.
  uint8_t hdr[5];
  hdr[0] = 1;
  memcpy(hdr + 1, &sequence_n, sizeof(sequence_n));

  uint32_t sent = 0;
  if (!packet_builder_send(conn, ml_result_chr_handle, hdr, sizeof(hdr),
      segments, 2, max_payload, &sent)) {
    return;
  }

  // Continuation packets: a single type byte 0.
  uint8_t cont_hdr = 0;
  while (sent < data_total) {
    if (!packet_builder_send(conn, ml_result_chr_handle, &cont_hdr, sizeof(cont_hdr),
        segments, 2, max_payload, &sent)) {
      return;
    }
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

