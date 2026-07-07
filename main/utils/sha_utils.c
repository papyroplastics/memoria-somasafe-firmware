#include <stdio.h>

#include <mbedtls/sha256.h>

#include "utils/sha_utils.h"

int sha256_stream_begin(struct sha256_stream *stream) {
  mbedtls_sha256_init(&stream->ctx);
  return mbedtls_sha256_starts(&stream->ctx, 0);
}

int sha256_stream_update(struct sha256_stream *stream,
    const uint8_t *data, size_t len) {
  if (data == NULL && len != 0) {
    return 1;
  }
  if (len == 0) {
    return 0;
  }
  return mbedtls_sha256_update(&stream->ctx, data, len);
}

int sha256_stream_final(struct sha256_stream *stream,
    uint8_t out[SHA256_DIGEST_LENGTH]) {
  int err = mbedtls_sha256_finish(&stream->ctx, out);
  mbedtls_sha256_free(&stream->ctx);
  return err;
}

int sha256_compute(const uint8_t *data, size_t len,
    uint8_t out[SHA256_DIGEST_LENGTH]) {
  struct sha256_stream stream;

  int err = sha256_stream_begin(&stream);
  if (err) goto abort;

  err = sha256_stream_update(&stream, data, len);
  if (err) goto abort;

  return sha256_stream_final(&stream, out);

abort:
  mbedtls_sha256_free(&stream.ctx);
  return err;
}

struct sha_str sha256_to_hex(const uint8_t checksum[SHA256_DIGEST_LENGTH]) {
  struct sha_str sha_str_i;
  for (unsigned int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
    sprintf(sha_str_i.hex + i * 2, "%02x", checksum[i]);
  }
  sha_str_i.hex[SHA256_DIGEST_LENGTH * 2] = '\0';
  return sha_str_i;
}
