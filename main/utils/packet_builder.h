#ifndef UTILS_PACKET_BUILDER
#define UTILS_PACKET_BUILDER

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <os/os_mbuf.h>

// A contiguous chunk of payload. The builder treats an array of segments as one
// logical byte stream concatenated in order.
struct packet_segment {
  const uint8_t *data;
  uint32_t len;
};

// Build a single notification packet: copy the header, then as much of the
// concatenated segment stream as fits in `max_payload`, starting at byte offset
// `sent`. The number of payload bytes copied is written to `body_len_out` so the
// caller can advance `sent`. Returns the packet mbuf, or NULL on
// allocation/copy failure.
struct os_mbuf *packet_builder_build(
    const uint8_t *header, uint8_t header_len,
    const struct packet_segment *segments, size_t segment_count,
    uint32_t sent, uint16_t max_payload,
    uint32_t *body_len_out);

// Build one packet (see packet_builder_build) and notify it over chr_handle,
// advancing `*sent` by the number of payload bytes sent. Returns false if the
// packet could not be built, so the caller can stop.
bool packet_builder_send(uint16_t conn, uint16_t chr_handle,
    const uint8_t *header, uint8_t header_len,
    const struct packet_segment *segments, size_t segment_count,
    uint16_t max_payload, uint32_t *sent);

#endif // UTILS_PACKET_BUILDER
