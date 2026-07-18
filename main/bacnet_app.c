#include "bacnet_app.h"

#include "esp_log.h"

static const char *TAG = "bacnet_app";

esp_err_t bacnet_app_init(
    bacnet_app_profile_callback_t profile_callback)
{
    (void)profile_callback;

    ESP_LOGI(TAG, "BACnet application module loaded");
    return ESP_OK;
}

esp_err_t bacnet_app_start(
    const bacnet_app_task_handle_refs_t *task_handles)
{
    (void)task_handles;
    return ESP_OK;
}

void bacnet_app_maintenance_1s(void)
{
}

void bacnet_app_send_mstp_i_am(void)
{
}

void bacnet_app_reset_mstp_diagnostics(void)
{
}

uint32_t bacnet_app_get_mstp_pdu_count(void)
{
    return 0;
}

bool bacnet_app_wifi_connected(void)
{
    return false;
}