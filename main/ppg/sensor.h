#ifndef PPG_SENSOR_H
#define PPG_SENSOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t ppg_get_hr(void);
void ppg_task(void *param);

#define PPG_SNAPSHOT_SECONDS 8
#define PPG_SAMPLE_RATE_HZ 32
#define PPG_SNAPSHOT_SAMPLES (PPG_SNAPSHOT_SECONDS * PPG_SAMPLE_RATE_HZ)
#define PPG_RING_CAPACITY 8

struct ppg_snapshot {
  uint16_t sample_count;
  uint32_t start_ms;
  uint32_t end_ms;
  float samples[PPG_SNAPSHOT_SAMPLES];
};

struct ppg_ring_buffer {
  pthread_mutex_t mutex;
  pthread_mutex_t read_mutex;
  pthread_mutex_t write_mutex;
  struct ppg_snapshot snapshots[PPG_RING_CAPACITY];
  size_t oldest_idx;
  size_t newest_idx;
  size_t read_idx;
  bool has_data;
};

bool ppg_ring_acquire_read(struct ppg_snapshot **snapshot_out);
void ppg_ring_release_read(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // PPG_SENSOR_H
