#ifndef UTILS_NOTIF_TRANSACTION
#define UTILS_NOTIF_TRANSACTION

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Reconstruction layer for sending an arbitrary-length payload across several
// MTU-bounded GATT notifications. A payload sent this way is a "transaction".
//
// Each notification carries a fixed 3-byte reconstruction header, independent
// of the service-specific payload, so a client can reframe any service the
// same way before running service-specific code:
//
//   flags          (1 byte) : bit0 START of transaction, bit1 END of
//                             transaction (both set on a single-packet
//                             transaction); other bits reserved.
//   transaction_id (1 byte) : equal across every packet of a transaction,
//                             incremented per transaction for error checking.
//   sequence_n     (1 byte) : 0 on the first packet, +1 per packet, wraps at
//                             256. Separate from any service-layer sequence
//                             number carried inside the payload.

#define NOTIF_TXN_FLAG_START 0x01u
#define NOTIF_TXN_FLAG_END   0x02u
#define NOTIF_TXN_HEADER_LEN 3u

// A contiguous chunk of payload. The builder treats an array of segments as one
// logical byte stream concatenated in order.
struct packet_segment {
  const uint8_t *data;
  uint32_t len;
};

// Per-transaction bookkeeping owned by the caller. Allocate one, keep it alive
// across every send that belongs to the same transaction, and never read or
// write its fields directly. Zero-initialize before first use. `active` only
// guards against misuse (continuing before starting, or starting twice).
struct transaction_state {
  uint8_t transaction_id;  // id shared by the current transaction's packets
  uint8_t sequence_n;      // next reconstruction sequence number to emit
  bool    active;          // true while a transaction is mid-flight
};

#define TRANSACTION_STATE_INIT ((struct transaction_state){0})

// Send the concatenated segment stream over chr_handle as part of a transaction,
// fragmenting it across as many notifications as the MTU requires and prepending
// the reconstruction header to each. The layer treats the segments as opaque
// bytes; any service framing (headers, tails) is just more segments.
//
// `start` opens a new transaction (fresh id, sequence reset); `end` closes it
// (END flag on the last packet) regardless of the send outcome. A single-call
// transaction passes both. Continuations pass neither.
//
// On a notification build/send failure the call aborts early and returns false;
// the client drops the transaction on the resulting sequence gap (no ACK).
bool notif_transaction_send(
    struct transaction_state *st,
    uint16_t conn, uint16_t chr_handle,
    const struct packet_segment *segments, size_t segment_count,
    uint16_t max_payload, bool start, bool end);

#endif // UTILS_NOTIF_TRANSACTION
