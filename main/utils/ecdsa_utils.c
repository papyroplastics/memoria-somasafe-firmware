#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>

#include "utils/ecdsa_utils.h"
#include "utils/sha_utils.h"

int ecdsa_sign(const uint8_t priv[ECDSA_P256_PRIVKEY_LENGTH],
    const uint8_t *data, size_t len, uint8_t *sig, size_t *sig_len) {
  uint8_t hash[SHA256_DIGEST_LENGTH];
  int err = sha256_compute(data, len, hash);
  if (err) return err;

  mbedtls_ecdsa_context ctx;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context drbg;
  mbedtls_ecdsa_init(&ctx);
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&drbg);

  err = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, NULL, 0);
  if (err) goto end;

  err = mbedtls_ecp_read_key(MBEDTLS_ECP_DP_SECP256R1, &ctx, priv,
      ECDSA_P256_PRIVKEY_LENGTH);
  if (err) goto end;

  err = mbedtls_ecdsa_write_signature(&ctx, MBEDTLS_MD_SHA256, hash,
      sizeof(hash), sig, ECDSA_SIG_MAX_LENGTH, sig_len,
      mbedtls_ctr_drbg_random, &drbg);

end:
  mbedtls_ecdsa_free(&ctx);
  mbedtls_ctr_drbg_free(&drbg);
  mbedtls_entropy_free(&entropy);
  return err;
}

int ecdsa_verify(const uint8_t pub[ECDSA_P256_PUBKEY_LENGTH],
    const uint8_t *data, size_t len, const uint8_t *sig, size_t sig_len) {
  uint8_t hash[SHA256_DIGEST_LENGTH];
  int err = sha256_compute(data, len, hash);
  if (err) return err;

  return ecdsa_verify_digest(pub, hash, sig, sig_len);
}

int ecdsa_verify_digest(const uint8_t pub[ECDSA_P256_PUBKEY_LENGTH],
    const uint8_t digest[SHA256_DIGEST_LENGTH], const uint8_t *sig,
    size_t sig_len) {
  int err;
  mbedtls_ecdsa_context ctx;
  mbedtls_ecp_group grp;
  mbedtls_ecp_point q;
  mbedtls_ecdsa_init(&ctx);
  mbedtls_ecp_group_init(&grp);
  mbedtls_ecp_point_init(&q);

  err = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
  if (err) goto end;

  err = mbedtls_ecp_point_read_binary(&grp, &q, pub, ECDSA_P256_PUBKEY_LENGTH);
  if (err) goto end;

  err = mbedtls_ecp_set_public_key(MBEDTLS_ECP_DP_SECP256R1, &ctx, &q);
  if (err) goto end;

  err = mbedtls_ecdsa_read_signature(&ctx, digest, SHA256_DIGEST_LENGTH, sig, sig_len);

end:
  mbedtls_ecdsa_free(&ctx);
  mbedtls_ecp_group_free(&grp);
  mbedtls_ecp_point_free(&q);
  return err;
}
