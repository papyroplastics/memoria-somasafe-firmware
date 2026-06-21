#ifndef ECDSA_UTILS_H
#define ECDSA_UTILS_H

#include <stddef.h>
#include <stdint.h>

// Raw P-256 key sizes: private is the 32-byte scalar, public is the 65-byte
// uncompressed point (0x04 || X || Y). Signatures are ASN.1/DER encoded.
#define ECDSA_P256_PRIVKEY_LENGTH 32
#define ECDSA_P256_PUBKEY_LENGTH 65
#define ECDSA_SIG_MAX_LENGTH 72

// Sign the SHA-256 digest of `data` with the P-256 private key `priv`.
// Writes the DER signature into `sig` (must hold ECDSA_SIG_MAX_LENGTH bytes)
// and stores its length in `sig_len`. Returns 0 on success.
int ecdsa_sign(const uint8_t priv[ECDSA_P256_PRIVKEY_LENGTH],
    const uint8_t *data, size_t len, uint8_t *sig, size_t *sig_len);

// Verify the DER signature `sig` over the SHA-256 digest of `data` against the
// P-256 public key `pub`. Returns 0 if the signature is valid.
int ecdsa_verify(const uint8_t pub[ECDSA_P256_PUBKEY_LENGTH],
    const uint8_t *data, size_t len, const uint8_t *sig, size_t sig_len);

#endif // ECDSA_UTILS_H
