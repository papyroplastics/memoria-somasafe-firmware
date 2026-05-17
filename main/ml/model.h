#ifndef ML_MODEL_H
#define ML_MODEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

enum ml_op_err {
  ML_ERR_NONE = 0,
  INSUFICCIENT_SPACE,
  SHA_HW_FAILURE,
  INVALID_MODEL,
};

extern uint8_t* model_buf;

enum ml_op_err model_write(const void* buf, size_t count);

size_t model_get_size(void);
enum ml_op_err model_set_size(size_t size);

size_t model_get_pos(void);
enum ml_op_err model_set_pos(size_t pos);

#define MODEL_CHECKSUM_LEN 32
enum ml_op_err model_get_checksum(const uint8_t **out_prt);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ML_MODEL_H
