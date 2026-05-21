#ifndef BLE_GAP_H
#define BLE_GAP_H

int ble_gap_task_prepare(void);
int ble_gap_advert_config();
int ble_gap_advert_start();
int ble_gap_advert_stop(void);

#endif  // BLE_GAP_H
