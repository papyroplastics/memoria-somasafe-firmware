#include <stdio.h>

#include <mbedtls/sha256.h>

#include "utils/sha_utils.h"

int sha256_compute(const uint8_t *data, size_t len,
    uint8_t out[SHA256_DIGEST_LENGTH]) {
  if (data == NULL && len != 0) {
    return 1;
  }

  int err = 0;
  mbedtls_sha256_context sha_ctx = {0};

  mbedtls_sha256_init(&sha_ctx);

  err = mbedtls_sha256_starts(&sha_ctx, 0);
  if (err) goto end;

  if (len != 0) {
    err = mbedtls_sha256_update(&sha_ctx, data, len);
    if (err) goto end;
  }

  err = mbedtls_sha256_finish(&sha_ctx, out);

end:
  mbedtls_sha256_free(&sha_ctx);
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
