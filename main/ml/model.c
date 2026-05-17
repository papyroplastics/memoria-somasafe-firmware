#include <string.h>
#include <stdint.h>
#include <esp_log.h>
#include <mbedtls/sha256.h>
#include <hal/sha_types.h>

#include "common.h"
#include "ml/model.h"

static const char tag[] = APP_TAG "-model";

uint8_t* model_buf = NULL;
static size_t model_buf_pos = 0;
static size_t model_buf_size = 0;

static bool sha256_dirty = true;
static uint8_t sha256_buf[MODEL_CHECKSUM_LEN];

enum ml_op_err model_write(const void* buf, size_t count) {
  if (model_buf_pos + count > model_buf_size || model_buf == NULL) {
    return INSUFICCIENT_SPACE;
  }

  memcpy(model_buf + model_buf_pos, buf, count);
  model_buf_pos += count;
  sha256_dirty = true;

  return ML_ERR_NONE;
}

size_t model_get_size(void) {
  return model_buf_size;
}

enum ml_op_err model_set_size(size_t size) {
  if (size == model_buf_size) {
    return ML_ERR_NONE;
  }

  sha256_dirty = true;

  if (model_buf != NULL) {
    free(model_buf);
  } 

  if (size == 0) {
    model_buf_pos = 0;
    model_buf = NULL;
    return ML_ERR_NONE;
  }

  model_buf = malloc(size);

  if (model_buf == NULL) {
    model_buf_size = 0;
    ESP_LOGE(tag, "failed to allocate %d bytes for model buffer", size);
    return INSUFICCIENT_SPACE;
  }

  model_buf_size = size;

  return ML_ERR_NONE;
}

size_t model_get_pos(void) {
  return model_buf_pos;
}

enum ml_op_err model_set_pos(size_t pos) {
  if (pos > model_buf_size) {
    model_buf_pos = model_buf_size;

  } else {
    model_buf_pos = pos;
  }

  return ML_ERR_NONE;
}

enum ml_op_err model_get_checksum(uint8_t const **out_ptr) {
  if (sha256_dirty) {
    int err = 0;
    mbedtls_sha256_context sha_ctx = {0};

    mbedtls_sha256_init(&sha_ctx);

    err = mbedtls_sha256_starts(&sha_ctx, 0);
    if (err) goto end;
    
    err = mbedtls_sha256_update(&sha_ctx, model_buf, model_buf_size);
    if (err) goto end;

    err = mbedtls_sha256_finish(&sha_ctx, sha256_buf);
    if (err) goto end;

  end:
    mbedtls_sha256_free(&sha_ctx);
    
    if (err) {
      return SHA_HW_FAILURE;
    }

    ESP_LOGI(tag, "computed SHA-256 of model %hhx%hhx%hhx%hhx%hhx%hhx%hhx%hhx",
        sha256_buf[0], sha256_buf[1], sha256_buf[2], sha256_buf[3],
        sha256_buf[4], sha256_buf[5], sha256_buf[6], sha256_buf[7]);
  }

  *out_ptr = sha256_buf;
  return ML_ERR_NONE;
}
