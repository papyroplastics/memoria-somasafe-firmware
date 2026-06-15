#ifndef BLE_GAP_H
#define BLE_GAP_H

#include <stdint.h>
#include <stdbool.h>

#include "ble/host.h"

#define MAC_ADDR_TYPE(type) type == BLE_ADDR_PUBLIC ? "static" : "random"
#define MAC_ADDR_STR "%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX"
#define MAC_ADDR_ITEM(addr) addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]

int ble_gap_task_prepare(void);
int ble_gap_advert_config(void);
int ble_gap_advert_start(void);
int ble_gap_advert_stop(void);

uint16_t ble_gap_get_conn_handle(void);
bool ble_gap_check_conn_encrypted(uint16_t conn_handle);

#if SMP_SECURITY_LEVEL != 0
#define ASSERT_ENCRYPYED() if (!ble_gap_check_conn_encrypted(conn)) return;
#else 
#define ASSERT_ENCRYPYED()
#endif

#endif  // BLE_GAP_H
