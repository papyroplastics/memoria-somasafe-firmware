#ifndef PPG_SENSOR_H
#define PPG_SENSOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t ppg_get_hr(void);
void ppg_task(void *param);

#define PPG_SLICE_SECONDS 8
#define PPG_SAMPLE_RATE_HZ 32
#define PPG_SLICE_SAMPLES (PPG_SLICE_SECONDS * PPG_SAMPLE_RATE_HZ)
#define PPG_RING_CAPACITY 8

struct ppg_slice {
  uint16_t sample_count;
  uint32_t start_ms;
  uint32_t end_ms;
  float samples[PPG_SLICE_SAMPLES];
};

bool ppg_ring_acquire_read(struct ppg_slice **slice_out);
void ppg_ring_release_read(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // PPG_SENSOR_H
