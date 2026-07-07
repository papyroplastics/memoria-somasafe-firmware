#ifndef SHA_UTILS_H
#define SHA_UTILS_H

#include <stddef.h>
#include <stdint.h>

#include <mbedtls/sha256.h>

#define SHA256_DIGEST_LENGTH 32

struct sha_str {
  char hex[SHA256_DIGEST_LENGTH * 2 + 1];
};

struct sha256_stream {
  mbedtls_sha256_context ctx;
};

// Incremental SHA-256: begin, update as data arrives, final to get the digest.
// begin can be called again at any point to discard and restart the stream.
int sha256_stream_begin(struct sha256_stream *stream);
int sha256_stream_update(struct sha256_stream *stream,
    const uint8_t *data, size_t len);
int sha256_stream_final(struct sha256_stream *stream,
    uint8_t out[SHA256_DIGEST_LENGTH]);

int sha256_compute(const uint8_t *data, size_t len,
    uint8_t out[SHA256_DIGEST_LENGTH]);
struct sha_str sha256_to_hex(const uint8_t checksum[SHA256_DIGEST_LENGTH]);

#endif // SHA_UTILS_H
