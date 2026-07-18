#include "sensor_service.h"

#include <string.h>

#include "esp_log.h"
#include "sen54.h"

#include "analog_input.h"
#include "binary_value.h"
#include "bacnet/bacenum.h"

/* Include the headers required by the current local DS18B20 implementation. */

static const char *TAG = "sensor_service";

static void sensor_service_task(void *parameter)
{
    (void)parameter;

    ESP_LOGI(TAG, "Sensor service task started");

    /*
     * One-time initialization here:
     * - DS18B20 GPIO initialization
     * - SEN54 initialization
     * - initial stabilization delay
     */

    for (;;) {
        /*
         * Existing sensor processing:
         * - BV1 reset command
         * - SEN54 measurement read
         * - AI1-AI7 updates
         * - DS18B20 read
         * - AI8 update
         * - reliability updates
         */

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

esp_err_t sensor_service_start(TaskHandle_t *task_handle)
{
    if (task_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    BaseType_t result = xTaskCreate(
        sensor_service_task,
        "sensors",
        4096,
        NULL,
        3,
        task_handle);

    if (result != pdPASS) {
        *task_handle = NULL;
        ESP_LOGE(TAG, "Failed to create sensor task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}