#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include "utils/ring_buffer.h"

static size_t next_slice_idx(const struct ring_buffer *rb, size_t idx) {
  return (idx + 1) % rb->capacity;
}

static void *ring_buffer_slice(const struct ring_buffer *rb, size_t idx) {
  return (uint8_t*)rb->slices + idx * rb->slice_size;
}

void *ring_buffer_acquire_write(struct ring_buffer *rb) {
  pthread_mutex_lock(&rb->mutex);

  size_t next_idx = 0;
  if (rb->newest_idx == SIZE_MAX) {
    rb->oldest_idx = 0;
  } else {
    next_idx = next_slice_idx(rb, rb->newest_idx);

    if (next_idx == rb->oldest_idx) {
      rb->oldest_idx = next_slice_idx(rb, rb->oldest_idx);
    }

    if (next_idx == rb->read_idx) {
      rb->read_idx = SIZE_MAX;

      // lock read mutex to make sure it's not being used
      if (pthread_mutex_trylock(&rb->read_mutex)) pthread_mutex_unlock(&rb->read_mutex);
    }
  }

  rb->newest_idx = next_idx;

  pthread_mutex_lock(&rb->write_mutex);
  pthread_mutex_unlock(&rb->mutex);
  return ring_buffer_slice(rb, rb->newest_idx);
}

void ring_buffer_release_write(struct ring_buffer *rb) {
  pthread_cond_signal(&rb->wait_cond);
  pthread_mutex_unlock(&rb->write_mutex);
}

bool ring_buffer_acquire_read(struct ring_buffer *rb, void **slice_out) {
  if (slice_out == NULL) {
    return false;
  }

  pthread_mutex_lock(&rb->mutex);
  if (rb->newest_idx == SIZE_MAX) {
    pthread_mutex_unlock(&rb->mutex);
    return false;
  }

  size_t candidate = rb->oldest_idx;
  if (rb->read_idx != SIZE_MAX) {
    candidate = next_slice_idx(rb, rb->read_idx);
  }

  if (candidate == rb->newest_idx) {
    pthread_mutex_unlock(&rb->mutex);
    return false;
  }

  rb->read_idx = candidate;
  pthread_mutex_lock(&rb->read_mutex);
  pthread_mutex_unlock(&rb->mutex);

  *slice_out = ring_buffer_slice(rb, candidate);
  return true;
}

void ring_buffer_release_read(struct ring_buffer *rb) {
  pthread_mutex_unlock(&rb->read_mutex);
}

void ring_buffer_wait_data(struct ring_buffer *rb) {
  pthread_mutex_lock(&rb->mutex);
  pthread_cond_wait(&rb->wait_cond, &rb->mutex);
  pthread_mutex_unlock(&rb->mutex);
}
