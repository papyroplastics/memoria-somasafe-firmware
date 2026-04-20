#ifndef GATT_H
#define GATT_H

#include <stdint.h>

int gatt_init(void);

void gatt_update_cccd_status(uint16_t conn_handle, uint16_t attr_handle,
                             uint8_t notification, uint8_t indication);

int gatt_hr_attr_signal();

#endif  // GATT_H
