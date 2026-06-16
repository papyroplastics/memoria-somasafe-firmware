#include <esp_log.h>
#include <host/ble_gatt.h>
#include <os/os_mbuf.h>

#include "common.h"
#include "utils/packet_builder.h"

static const char tag[] = APP_TAG "-packet-builder";

struct os_mbuf *packet_builder_build(
    const uint8_t *header, uint8_t header_len,
    const struct packet_segment *segments, size_t segment_count,
    uint32_t sent, uint16_t max_payload,
    uint32_t *body_len_out) {

  uint32_t data_total = 0;
  for (size_t i = 0; i < segment_count; i++) {
    data_total += segments[i].len;
  }

  uint16_t body_capacity = max_payload - header_len;
  uint32_t body_len = data_total - sent;
  if (body_len > body_capacity) body_len = body_capacity;

  struct os_mbuf *om = os_msys_get_pkthdr(header_len + body_len, 0);
  if (om == NULL) {
    return NULL;
  }

  if (os_mbuf_append(om, header, header_len) != 0) {
    os_mbuf_free_chain(om);
    return NULL;
  }

  // Walk the segments and copy the [sent, sent + body_len) slice of the
  // concatenated stream, which may span a segment boundary.
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

  if (body_len_out != NULL) {
    *body_len_out = body_len;
  }

  return om;
}

bool packet_builder_send(uint16_t conn, uint16_t chr_handle,
    const uint8_t *header, uint8_t header_len,
    const struct packet_segment *segments, size_t segment_count,
    uint16_t max_payload, uint32_t *sent) {

  uint32_t body_len = 0;
  struct os_mbuf *om = packet_builder_build(header, header_len, segments,
      segment_count, *sent, max_payload, &body_len);
  if (om == NULL) {
    ESP_LOGE(tag, "failed to build notification packet");
    return false;
  }

  int err = ble_gatts_notify_custom(conn, chr_handle, om);
  if (err != 0) {
    ESP_LOGW(tag, "notification failed with reason %d", err);
  }

  *sent += body_len;
  return true;
}
