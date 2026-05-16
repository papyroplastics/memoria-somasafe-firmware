#ifndef ML_MODEL_H
#define ML_MODEL_H

#include <stddef.h>
#include <stdint.h>

enum model_load_err {
  NONE = 0,
  INSUFICCIENT_SPACE = 1,
  SHA_HW_FAILURE = 3,
};

enum model_load_err model_write(const void* buf, size_t count);
enum model_load_err model_set_size(size_t size);
enum model_load_err model_set_pos(size_t pos);
size_t model_get_size(void);
size_t model_get_pos(void);

/**
 * @param output 32-byte long writable buffer 
 */
enum model_load_err model_get_checksum(uint8_t *out_buf);

#endif // ML_MODEL_H
