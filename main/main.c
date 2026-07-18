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
#include "sensor_service.h"
#include "User_Settings.h"
#include "app_storage.h"
#include "bacnet/basic/object/ai.h"


static const char *TAG = "bacnet";

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


static TaskHandle_t bacnet_rx_task_handle = NULL;
static TaskHandle_t bacnet_mstp_rx_task_handle = NULL;
static TaskHandle_t bacnet_core_task_handle = NULL;
static TaskHandle_t bacnet_cov_task_handle = NULL;
static TaskHandle_t sen54_task_handle = NULL;



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
    ESP_ERROR_CHECK(app_storage_init());

    stack_profile_init();

    ESP_ERROR_CHECK(bacnet_app_init(bacnet_profile_callback));

    ESP_LOGI(TAG, "Initializing display");
    display_init();

    /* Remaining task startup and supervisor loop */


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
            stack_profiler_log_report();
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}