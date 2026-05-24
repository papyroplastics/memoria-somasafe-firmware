#ifndef BLE_GAP_H
#define BLE_GAP_H

#include <stdint.h>

int ble_gap_task_prepare(void);
int ble_gap_advert_config();
int ble_gap_advert_start();
int ble_gap_advert_stop(void);

uint16_t ble_gap_get_conn_handle(void);

#endif  // BLE_GAP_H
