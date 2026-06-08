#ifndef PPG_SENSOR_H
#define PPG_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PPG_SAMPLE_RATE     64
#define PPG_ACC_RATE        32
#define PPG_SLICE_SECONDS    8
#define PPG_SLICE_PPG_COUNT (PPG_SAMPLE_RATE * PPG_SLICE_SECONDS)  // 512
#define PPG_SLICE_ACC_COUNT (PPG_ACC_RATE    * PPG_SLICE_SECONDS)  // 256

#define PPG_RING_CAPACITY 4

struct ppg_slice {
  uint32_t start_ms;
  uint32_t end_ms;
  float    ppg[PPG_SLICE_PPG_COUNT];
  float    acc[PPG_SLICE_ACC_COUNT];
};

float sensor_get_ppg(void);
float sensor_get_acc(void);

void ppg_task(void *param);

bool ppg_ring_acquire_read(struct ppg_slice **slice_out);
void ppg_ring_release_read(void);

#ifdef __cplusplus
}
#endif

#endif  // PPG_SENSOR_H
