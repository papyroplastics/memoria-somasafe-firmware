#ifndef SHA_UTILS_H
#define SHA_UTILS_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_DIGEST_LENGTH 32

struct sha_str {
  char hex[SHA256_DIGEST_LENGTH * 2 + 1];
};

int sha256_compute(const uint8_t *data, size_t len,
    uint8_t out[SHA256_DIGEST_LENGTH]);
struct sha_str sha256_to_hex(const uint8_t checksum[SHA256_DIGEST_LENGTH]);

#endif // SHA_UTILS_H
