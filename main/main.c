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
#include "app_storage.h"
#include "stack_profiler.h"
#include "app_supervisor.h"

static const char *TAG = "bacnet";

static TaskHandle_t bacnet_rx_task_handle = NULL;
static TaskHandle_t bacnet_mstp_rx_task_handle = NULL;
static TaskHandle_t bacnet_core_task_handle = NULL;
static TaskHandle_t bacnet_cov_task_handle = NULL;
static TaskHandle_t sen54_task_handle = NULL;

void app_main(void)
{
    ESP_ERROR_CHECK(app_storage_init());

            const stack_profiler_task_handle_refs_t
                profiler_task_handles = {
                    .bacnet_rx = &bacnet_rx_task_handle,
                    .bacnet_mstp_rx =
                        &bacnet_mstp_rx_task_handle,
                    .bacnet_core = &bacnet_core_task_handle,
                    .bacnet_cov = &bacnet_cov_task_handle,
                    .sensor = &sen54_task_handle,
                };

            stack_profiler_init(&profiler_task_handles);

            ESP_ERROR_CHECK(
                bacnet_app_init(
                    stack_profiler_bacnet_callback));

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

    app_supervisor_run();

}