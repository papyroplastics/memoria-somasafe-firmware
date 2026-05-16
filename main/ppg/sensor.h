#ifndef PPG_SENSOR_H
#define PPG_SENSOR_H

#include <stdint.h>

uint8_t ppg_get_hr(void);
void ppg_task(void *param);

#endif  // PPG_SENSOR_H
