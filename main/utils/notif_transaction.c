#include <esp_log.h>
#include <host/ble_gatt.h>
#include <os/os_mbuf.h>

#include "common.h"
#include "utils/notif_transaction.h"

static const char tag[] = APP_TAG "-notif-transaction";

// Build one notification: the 3-byte reconstruction header followed by the
// [sent, sent + body_len) slice of the concatenated segment stream, which may
// span a segment boundary. Returns the mbuf, or NULL on allocation/copy failure.
static struct os_mbuf *build_packet(
    const uint8_t header[NOTIF_TXN_HEADER_LEN],
    const struct packet_segment *segments, size_t segment_count,
    uint32_t sent, uint32_t body_len) {

  struct os_mbuf *om = os_msys_get_pkthdr(NOTIF_TXN_HEADER_LEN + body_len, 0);
  if (om == NULL) {
    return NULL;
  }

  if (os_mbuf_append(om, header, NOTIF_TXN_HEADER_LEN) != 0) {
    os_mbuf_free_chain(om);
    return NULL;
  }

  uint32_t pos = sent;
  uint32_t remain = body_len;
  uint32_t base = 0;
  for (size_t i = 0; i < segment_count && remain > 0; i++) {
    uint32_t seg_end = base + segments[i].len;
    if (pos < seg_end) {
      uint32_t seg_offset = pos - base;
      uint32_t n = segments[i].len - seg_offset;
      if (n > remain) n = remain;

      if (os_mbuf_append(om, segments[i].data + seg_offset, n) != 0) {
        os_mbuf_free_chain(om);
        return NULL;
      }

      pos += n;
      remain -= n;
    }
    base = seg_end;
  }

  return om;
}

bool notif_transaction_send(
    struct transaction_state *st,
    uint16_t conn, uint16_t chr_handle,
    const struct packet_segment *segments, size_t segment_count,
    size_t start_segment_count,
    uint16_t max_payload, bool end) {

  if (max_payload <= NOTIF_TXN_HEADER_LEN) {
    ESP_LOGE(tag, "MTU too small for a reconstruction header");
    return false;
  }

  bool start = !st->active;
  if (start) {
    st->transaction_id++;
    st->sequence_n = 0;
    st->active = true;
  }

  // Start-only segments (service metadata) ride only on the first packet of the
  // transaction; continuations carry just the body segments.
  const struct packet_segment *body = start ? segments : segments + start_segment_count;
  size_t body_count = start ? segment_count : segment_count - start_segment_count;

  uint32_t data_total = 0;
  for (size_t i = 0; i < body_count; i++) {
    data_total += body[i].len;
  }

  uint16_t body_capacity = max_payload - NOTIF_TXN_HEADER_LEN;

  bool ok = true;
  uint32_t sent = 0;

  // At least one packet is always emitted so the START/END flags are delivered,
  // even when this call carries no body bytes (e.g. an empty terminating call).
  do {
    uint32_t body_len = data_total - sent;
    if (body_len > body_capacity) body_len = body_capacity;

    bool first_packet = (sent == 0) && start;
    bool last_packet = (sent + body_len >= data_total);

    uint8_t flags = 0;
    if (first_packet) flags |= NOTIF_TXN_FLAG_START;
    if (last_packet && end) flags |= NOTIF_TXN_FLAG_END;

    uint8_t header[NOTIF_TXN_HEADER_LEN] = { flags, st->transaction_id, st->sequence_n };

    struct os_mbuf *om = build_packet(header, body, body_count, sent, body_len);
    if (om == NULL) {
      ESP_LOGE(tag, "failed to build notification packet");
      ok = false;
      break;
    }

    int err = ble_gatts_notify_custom(conn, chr_handle, om);
    if (err != 0) {
      ESP_LOGW(tag, "notification failed with reason %d, aborting transaction", err);
      ok = false;
      break;
    }

    st->sequence_n++;
    sent += body_len;
  } while (sent < data_total);

  if (end) st->active = false;
  return ok;
}
