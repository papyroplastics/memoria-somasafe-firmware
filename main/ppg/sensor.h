#ifndef PPG_SENSOR_H
#define PPG_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t ppg_get_hr(void);
void ppg_task(void *param);

#define PPG_N_FEATURES    17
#define PPG_SLICE_WINDOWS 64
#define PPG_RING_CAPACITY  4

struct ppg_slice {
  uint16_t sample_count;
  uint32_t start_ms;
  uint32_t end_ms;
  float    samples[PPG_SLICE_WINDOWS * PPG_N_FEATURES];
};

bool ppg_ring_acquire_read(struct ppg_slice **slice_out);
void ppg_ring_release_read(void);

#ifdef __cplusplus
}
#endif

#endif  // PPG_SENSOR_H
