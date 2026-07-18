/* Minimal example: connect to Wi-Fi and initialize BACnet. */
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "analog_input.h"
#include "analog_value.h"
#include "bacnet_app.h"
#include "binary_input.h"
#include "binary_output.h"
#include "binary_value.h"
#include "display.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sensor_service.h"
#include "User_Settings.h"

#include "bacnet/basic/object/ai.h"

static const char *TAG = "bacnet";

int override_nvs_on_flash = 0;  /* Exported for AV/BV modules */

typedef enum {
    STACK_EVT_NORMAL = 0,
    STACK_EVT_BACNET_IP_RX,
    STACK_EVT_MSTP_RX,
    STACK_EVT_READ_PROPERTY,
    STACK_EVT_WRITE_PROPERTY,
    STACK_EVT_COV_NOTIFICATION,
    STACK_EVT_DISPLAY_UPDATE,
    STACK_EVT_SEN54_READ,
    STACK_EVT_COUNT
} stack_profile_event_t;

typedef struct {
    const char *task_name;
    TaskHandle_t *task_handle;
    uint32_t configured_stack_words;
    UBaseType_t last_hwm_words;
    UBaseType_t min_free_words_observed;
    UBaseType_t min_free_words_by_event[STACK_EVT_COUNT];
} stack_profile_task_t;

static TaskHandle_t bacnet_rx_task_handle = NULL;
static TaskHandle_t bacnet_mstp_rx_task_handle = NULL;
static TaskHandle_t bacnet_core_task_handle = NULL;
static TaskHandle_t bacnet_cov_task_handle = NULL;
static TaskHandle_t sen54_task_handle = NULL;

static stack_profile_task_t stack_profile_tasks[] = {
    {
        .task_name = "bacnet_rx",
        .task_handle = &bacnet_rx_task_handle,
        .configured_stack_words = 16384,
    },
    {
        .task_name = "bacnet_mstp_rx",
        .task_handle = &bacnet_mstp_rx_task_handle,
        .configured_stack_words = 12288,
    },
    {
        .task_name = "bacnet_core",
        .task_handle = &bacnet_core_task_handle,
        .configured_stack_words = 12288,
    },
    {
        .task_name = "bacnet_cov",
        .task_handle = &bacnet_cov_task_handle,
        .configured_stack_words = 24576,
    },
    {
        .task_name = "sen54",
        .task_handle = &sen54_task_handle,
        .configured_stack_words = 4096,
    },
};

#define STACK_PROFILE_TASK_COUNT \
    ((int)(sizeof(stack_profile_tasks) / sizeof(stack_profile_tasks[0])))

static const char *stack_profile_event_name(stack_profile_event_t event)
{
    switch (event) {
        case STACK_EVT_NORMAL:
            return "normal";
        case STACK_EVT_BACNET_IP_RX:
            return "bacnet_ip_rx";
        case STACK_EVT_MSTP_RX:
            return "mstp_rx";
        case STACK_EVT_READ_PROPERTY:
            return "read_property";
        case STACK_EVT_WRITE_PROPERTY:
            return "write_property";
        case STACK_EVT_COV_NOTIFICATION:
            return "cov_notification";
        case STACK_EVT_DISPLAY_UPDATE:
            return "display_update";
        case STACK_EVT_SEN54_READ:
            return "sen54_read";
        default:
            return "unknown";
    }
}

static void stack_profile_init(void)
{
    for (int i = 0; i < STACK_PROFILE_TASK_COUNT; i++) {
        stack_profile_tasks[i].last_hwm_words = 0;
        stack_profile_tasks[i].min_free_words_observed = UINT_MAX;
        for (int e = 0; e < STACK_EVT_COUNT; e++) {
            stack_profile_tasks[i].min_free_words_by_event[e] = UINT_MAX;
        }
    }
}

static void stack_profile_sample(stack_profile_event_t event)
{
    for (int i = 0; i < STACK_PROFILE_TASK_COUNT; i++) {
        TaskHandle_t handle = *stack_profile_tasks[i].task_handle;
        if (!handle) {
            continue;
        }

        UBaseType_t hwm = uxTaskGetStackHighWaterMark(handle);
        stack_profile_tasks[i].last_hwm_words = hwm;
        if (hwm < stack_profile_tasks[i].min_free_words_observed) {
            stack_profile_tasks[i].min_free_words_observed = hwm;
        }
        if (event >= 0 && event < STACK_EVT_COUNT &&
            hwm < stack_profile_tasks[i].min_free_words_by_event[event]) {
            stack_profile_tasks[i].min_free_words_by_event[event] = hwm;
        }
    }
}

static float stack_profile_used_percent(uint32_t configured_words, UBaseType_t min_free_words)
{
    if (configured_words == 0) {
        return 0.0f;
    }

    uint32_t used_words = configured_words;
    if (min_free_words < configured_words) {
        used_words = configured_words - (uint32_t)min_free_words;
    }

    return (100.0f * (float)used_words) / (float)configured_words;
}

static void stack_profile_log_report(void)
{
    ESP_LOGI(TAG, "==== STACK PROFILE REPORT ====");
    for (int i = 0; i < STACK_PROFILE_TASK_COUNT; i++) {
        stack_profile_task_t *entry = &stack_profile_tasks[i];
        TaskHandle_t handle = *entry->task_handle;
        if (!handle) {
            ESP_LOGW(TAG, "stack task=%s not running", entry->task_name);
            continue;
        }

        UBaseType_t hwm_now = uxTaskGetStackHighWaterMark(handle);
        if (hwm_now < entry->min_free_words_observed) {
            entry->min_free_words_observed = hwm_now;
        }

        float used_pct = stack_profile_used_percent(
            entry->configured_stack_words,
            entry->min_free_words_observed);
        uint32_t min_free_bytes = (uint32_t)entry->min_free_words_observed * sizeof(StackType_t);

        ESP_LOGI(TAG,
            "stack task=%s configured=%lu words (%lu bytes) hwm_now=%lu words (%lu bytes) min_free_observed=%lu words (%lu bytes) used=%.1f%%",
            entry->task_name,
            (unsigned long)entry->configured_stack_words,
            (unsigned long)(entry->configured_stack_words * sizeof(StackType_t)),
            (unsigned long)hwm_now,
            (unsigned long)(hwm_now * sizeof(StackType_t)),
            (unsigned long)entry->min_free_words_observed,
            (unsigned long)min_free_bytes,
            used_pct);
    }

    for (int e = 0; e < STACK_EVT_COUNT; e++) {
        stack_profile_event_t event = (stack_profile_event_t)e;
        for (int i = 0; i < STACK_PROFILE_TASK_COUNT; i++) {
            stack_profile_task_t *entry = &stack_profile_tasks[i];
            UBaseType_t min_event = entry->min_free_words_by_event[e];
            if (min_event == UINT_MAX) {
                continue;
            }
            float used_pct = stack_profile_used_percent(entry->configured_stack_words, min_event);
            ESP_LOGI(TAG,
                "stack_event event=%s task=%s min_free=%lu words (%lu bytes) used=%.1f%%",
                stack_profile_event_name(event),
                entry->task_name,
                (unsigned long)min_event,
                (unsigned long)(min_event * sizeof(StackType_t)),
                used_pct);
        }
    }
}

static void bacnet_profile_callback(bacnet_app_profile_event_t event)
{
    switch (event) {
        case BACNET_APP_PROFILE_BIP_RX:
            stack_profile_sample(STACK_EVT_BACNET_IP_RX);
            break;
        case BACNET_APP_PROFILE_MSTP_RX:
            stack_profile_sample(STACK_EVT_MSTP_RX);
            break;
        case BACNET_APP_PROFILE_READ_PROPERTY:
            stack_profile_sample(STACK_EVT_READ_PROPERTY);
            break;
        case BACNET_APP_PROFILE_WRITE_PROPERTY:
            stack_profile_sample(STACK_EVT_WRITE_PROPERTY);
            break;
        case BACNET_APP_PROFILE_COV_NOTIFICATION:
            stack_profile_sample(STACK_EVT_COV_NOTIFICATION);
            break;
        default:
            break;
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();

    stack_profile_init();

    override_nvs_on_flash = USER_OVERRIDE_NVS_ON_FLASH;
    if (override_nvs_on_flash) {
        ESP_LOGI(TAG, "Override flag set - erasing NVS to reset to defaults");
        nvs_flash_erase();
        ret = nvs_flash_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to reinitialize NVS after erase: %d", ret);
        } else {
            ESP_LOGI(TAG, "NVS reinitialized successfully");
        }
    } else if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "NVS needs initialization");
        nvs_flash_erase();
        nvs_flash_init();
    } else if (ret == ESP_OK) {
        ESP_LOGI(TAG, "NVS initialized from existing data");
    }

    if (bacnet_app_init(bacnet_profile_callback) != ESP_OK) {
        ESP_LOGE(TAG, "BACnet app initialization failed");
        return;
    }

    ESP_LOGI(TAG, "Initializing display");
    display_init();

    bacnet_app_task_handle_refs_t task_handles = {
        .bip_rx = &bacnet_rx_task_handle,
        .mstp_rx = &bacnet_mstp_rx_task_handle,
        .core = &bacnet_core_task_handle,
        .cov = &bacnet_cov_task_handle,
    };
    if (bacnet_app_start(&task_handles) != ESP_OK) {
        ESP_LOGE(TAG, "BACnet task start reported an error");
    }

    ESP_ERROR_CHECK(sensor_service_start(&sen54_task_handle));

    if (USER_ENABLE_BACNET_MSTP) {
        ESP_LOGI(TAG, "BACnet MS/TP ready");
    }

    uint32_t display_tick = 0;
    uint32_t mstp_last_seen_pdu = 0;
    uint8_t mstp_alive_ticks = 0;
    uint32_t stack_report_tick = 0;
    while (1) {
        bacnet_app_maintenance_1s();
        stack_profile_sample(STACK_EVT_NORMAL);

        uint32_t mstp_pdu_count = bacnet_app_get_mstp_pdu_count();
        if (USER_ENABLE_BACNET_MSTP) {
            if (mstp_pdu_count != mstp_last_seen_pdu) {
                mstp_last_seen_pdu = mstp_pdu_count;
                mstp_alive_ticks = 6;
            } else if (mstp_alive_ticks > 0) {
                mstp_alive_ticks--;
            }
        } else {
            mstp_alive_ticks = 0;
        }

        display_set_link_status(
            bacnet_app_wifi_connected(),
            USER_ENABLE_BACNET_MSTP && (mstp_alive_ticks > 0));

        if (++display_tick % 2 == 0) {
            float ai1_temp = Analog_Input_Present_Value(1);
            float ai2_humidity = Analog_Input_Present_Value(2);
            float ai3_voc = Analog_Input_Present_Value(3);
            float ai5_pm25 = Analog_Input_Present_Value(5);
            float ai8_ds18b20_temp = Analog_Input_Present_Value(8);

            stack_profile_sample(STACK_EVT_DISPLAY_UPDATE);

            display_update_values(
                ai5_pm25,
                ai1_temp,
                ai2_humidity,
                ai3_voc,
                ai8_ds18b20_temp);

            stack_profile_sample(STACK_EVT_DISPLAY_UPDATE);
        }

        if (++stack_report_tick % 30 == 0) {
            stack_profile_log_report();
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}