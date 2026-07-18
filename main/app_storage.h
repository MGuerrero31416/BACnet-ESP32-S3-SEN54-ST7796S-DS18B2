#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Used by the BACnet object modules to decide whether persisted
 * NVS values should be loaded or replaced by compiled defaults.
 */
extern int override_nvs_on_flash;

/*
 * Initialize NVS and apply USER_OVERRIDE_NVS_ON_FLASH.
 */
esp_err_t app_storage_init(void);

#ifdef __cplusplus
}
#endif