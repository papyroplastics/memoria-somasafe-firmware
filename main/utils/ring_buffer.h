#ifndef UTILS_RING_BUFFER_H
#define UTILS_RING_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

// Generic single-producer/single-consumer ring buffer over fixed-size slices.
struct ring_buffer {
  pthread_mutex_t mutex;
  pthread_mutex_t read_mutex;
  pthread_mutex_t write_mutex;
  pthread_cond_t wait_cond;
  void *slices;
  size_t slice_size;
  size_t capacity;
  size_t oldest_idx;
  size_t newest_idx;
  size_t read_idx;
};

// Define a ring buffer and its backing storage statically.
#define RING_BUFFER_STATIC(type, name, count)        \
  static type name##_slices[count];                  \
  static struct ring_buffer name = {                 \
    .mutex = PTHREAD_MUTEX_INITIALIZER,              \
    .read_mutex = PTHREAD_MUTEX_INITIALIZER,         \
    .write_mutex = PTHREAD_MUTEX_INITIALIZER,        \
    .wait_cond = PTHREAD_COND_INITIALIZER,           \
    .slices = name##_slices,                         \
    .slice_size = sizeof(type),                      \
    .capacity = (count),                             \
    .oldest_idx = 0,                                 \
    .newest_idx = SIZE_MAX,                          \
    .read_idx = SIZE_MAX,                            \
  }

void *ring_buffer_acquire_write(struct ring_buffer *rb);
void ring_buffer_release_write(struct ring_buffer *rb);

bool ring_buffer_acquire_read(struct ring_buffer *rb, void **slice_out);
void ring_buffer_release_read(struct ring_buffer *rb);

void ring_buffer_wait_data(struct ring_buffer *rb);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif // UTILS_RING_BUFFER_H
