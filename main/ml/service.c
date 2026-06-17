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
#include "utils/notif_transaction.h"

struct ble_client_buffer ml_model_buffer = BLE_CLIENT_BUFFER_INIT("model");

// Each result is a complete single-call transaction; the state persists across
// calls only to advance the transaction id.
static struct transaction_state ml_result_tx = TRANSACTION_STATE_INIT;

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

  // Service header (start-only): the slice sequence number.
  uint8_t svc_hdr[sizeof(sequence_n)];
  memcpy(svc_hdr, &sequence_n, sizeof(sequence_n));

  const struct packet_segment segments[] = {
    { svc_hdr, sizeof(svc_hdr) },
    { (const uint8_t *)features, features_len },
    { (const uint8_t *)result,   result_len },
  };

  notif_transaction_send(&ml_result_tx, conn, ml_result_chr_handle,
      segments, 3, /*start_segment_count=*/1, max_payload, /*end=*/true);
}

void ml_error_notify_send(enum ml_error_code code) {
  if (!ml_errors_chr_notify) return;

  uint16_t conn = ble_gap_get_conn_handle();
  if (conn == BLE_HS_CONN_HANDLE_NONE) return;

  ASSERT_ENCRYPYED()

  uint8_t error = (uint8_t)code;
  ble_gatt_notify_chr(ml_errors_chr_handle, &error, sizeof(error));
}

