#ifndef COMMON_H
#define COMMON_H

// Project config
#include "sdkconfig.h"

// ESP-IDF APIs
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_log.h>
#include <nvs_flash.h>

// STD APIs
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

// NimBLE stack APIs
#include <nimble/ble.h>
#include <host/ble_hs.h>
#include <host/ble_uuid.h>

extern const char device_name[];
extern const char device_name_short[];
extern const uint16_t device_appearance;

#endif  // COMMON_H
