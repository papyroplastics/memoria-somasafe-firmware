#include <string.h>

#include <mbedtls/sha256.h>
#include <hal/sha_types.h>

#include "ml/model.h"

static size_t model_buf_pos = 0;
static size_t model_buf_size = 0;
static uint8_t* model_buf = NULL;

enum model_load_err model_write(const void* buf, size_t count) {
  if (model_buf_pos + count > model_buf_size || model_buf == NULL) {
    return INSUFICCIENT_SPACE;
  }

  memcpy(model_buf + model_buf_pos, buf, count);
  model_buf_pos += count;

  return NONE;
}

enum model_load_err model_set_size(size_t size) {
  if (model_buf != NULL) {
    free(model_buf);
  }

  model_buf_pos = 0;

  if (size != 0) {
    model_buf = malloc(size);

    if (model_buf == NULL) {
      model_buf_size = 0;
      return INSUFICCIENT_SPACE;
    }
  }

  model_buf_size = size;

  return NONE;
}

enum model_load_err model_set_pos(size_t pos) {
  if (pos > model_buf_size) {
    model_buf_pos = model_buf_size;

  } else {
    model_buf_pos = pos;
  }

  return NONE;
}

size_t model_get_size(void) {
  return model_buf_size;
}

size_t model_get_pos(void) {
  return model_buf_pos;
}

/**
 * @param output 32-byte long writable buffer 
 */
enum model_load_err model_get_checksum(uint8_t *out_buf) {
  int err = 0;
  mbedtls_sha256_context sha_ctx = {0};

  mbedtls_sha256_init(&sha_ctx);

  err = mbedtls_sha256_starts(&sha_ctx, 0);
  if (err) goto end;
  
  err = mbedtls_sha256_update(&sha_ctx, model_buf, model_buf_size);
  if (err) goto end;

  err = mbedtls_sha256_finish(&sha_ctx, out_buf);
  if (err) goto end;

end:
  mbedtls_sha256_free(&sha_ctx);
  return err ? SHA_HW_FAILURE : NONE;
}
