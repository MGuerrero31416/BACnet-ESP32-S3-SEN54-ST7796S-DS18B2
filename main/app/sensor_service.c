#include "sensor_service.h"

#include "esp_log.h"

#include "sen54.h"
#include "ds18b20.h"

#include "bacnet/basic/object/ai.h"
#include "bacnet/basic/object/bv.h"
#include "bacnet/bacenum.h"

static const char *TAG = "sensor_service";

/*
 * Sensor-to-BACnet mapping:
 *
 * AI1 = SEN54 temperature
 * AI2 = SEN54 relative humidity
 * AI3 = SEN54 VOC index
 * AI4 = SEN54 PM1.0
 * AI5 = SEN54 PM2.5
 * AI6 = SEN54 PM4.0
 * AI7 = SEN54 PM10
 * AI8 = DS18B20 temperature
 *
 * BV1 ACTIVE = SEN54 full reset
 */
static void sensor_service_task(void *parameter)
{
    (void)parameter;

    float ds18b20_temperature = 0.0f;
    sen54_data_t sensor_data;

    ESP_LOGI(TAG, "Sensor service task started");
    ESP_LOGI(TAG, "Initializing SEN54");

    sen54_init();

    /*
     * Allow the SEN54 fan and particle chamber to stabilize
     * before requesting the first measurement.
     */
    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGI(TAG, "Sensor acquisition started");

    for (;;) {
        /*
         * BV1 written ACTIVE triggers a full SEN54 reset.
         */
        if (Binary_Value_Present_Value(1) == BINARY_ACTIVE) {
            ESP_LOGI(TAG, "BV1 ACTIVE: sending SEN54 full reset");

            esp_err_t err = sen54_full_reset();

            ESP_LOGI(
                TAG,
                "SEN54 full reset %s",
                err == ESP_OK ? "OK" : "FAILED");

            Binary_Value_Present_Value_Set(
                1,
                BINARY_INACTIVE);

            /*
             * Give the sensor time before the next normal read.
             */
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        /*
         * Read SEN54 and update AI1-AI7.
         */
        if (sen54_read(&sensor_data)) {
            Analog_Input_Present_Value_Set(
                1,
                sensor_data.temperature);

            Analog_Input_Present_Value_Set(
                2,
                sensor_data.humidity);

            Analog_Input_Present_Value_Set(
                3,
                sensor_data.voc_index);

            Analog_Input_Present_Value_Set(
                4,
                sensor_data.pm1_0);

            Analog_Input_Present_Value_Set(
                5,
                sensor_data.pm2_5);

            Analog_Input_Present_Value_Set(
                6,
                sensor_data.pm4_0);

            Analog_Input_Present_Value_Set(
                7,
                sensor_data.pm10);

            for (uint32_t instance = 1; instance <= 7; instance++) {
                Analog_Input_Reliability_Set(
                    instance,
                    RELIABILITY_NO_FAULT_DETECTED);
            }

            ESP_LOGD(
                TAG,
                "SEN54: T=%.2f C RH=%.2f %% VOC=%.1f "
                "PM1=%.1f PM2.5=%.1f PM4=%.1f PM10=%.1f",
                sensor_data.temperature,
                sensor_data.humidity,
                sensor_data.voc_index,
                sensor_data.pm1_0,
                sensor_data.pm2_5,
                sensor_data.pm4_0,
                sensor_data.pm10);
        } else {
            for (uint32_t instance = 1; instance <= 7; instance++) {
                Analog_Input_Reliability_Set(
                    instance,
                    RELIABILITY_UNRELIABLE_OTHER);
            }

            ESP_LOGW(TAG, "SEN54 read failed");
        }

        /*
         * Read DS18B20 and update AI8.
         */
        if (ds18b20_read_temperature(&ds18b20_temperature)) {
            Analog_Input_Present_Value_Set(
                8,
                ds18b20_temperature);

            Analog_Input_Reliability_Set(
                8,
                RELIABILITY_NO_FAULT_DETECTED);

            ESP_LOGD(
                TAG,
                "DS18B20: T=%.2f C",
                ds18b20_temperature);
        } else {
            Analog_Input_Reliability_Set(
                8,
                RELIABILITY_UNRELIABLE_OTHER);

            ESP_LOGW(TAG, "DS18B20 read failed");
        }

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