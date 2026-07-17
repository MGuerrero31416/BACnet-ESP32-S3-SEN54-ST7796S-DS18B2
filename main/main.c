/* Minimal example: connect to Wi‑Fi and initialize BACnet. */
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "wifi_helper.h"
#include "display.h"
#include "analog_value.h"
#include "binary_value.h"
#include "analog_input.h"
#include "binary_input.h"
#include "binary_output.h"
#include "sen54.h"
#include "mstp_rs485.h"
#include "User_Settings.h"
#include "bacnet_dispatcher_config.h"
#include "bacnet_coordinator.h"
#include "bacnet_event_bus.h"

/* bacnet-stack headers */
#include "bacnet/basic/object/device.h"
#include "bacnet/basic/object/av.h"
#include "bacnet/basic/object/bv.h"
#include "bacnet/basic/object/ai.h"
#include "bacnet/basic/object/bi.h"
#include "bacnet/basic/object/bo.h"
#include "bacnet/basic/service/s_iam.h"
#include "bacnet/basic/tsm/tsm.h"
#include "bacnet/bacaddr.h"
#include "bacnet/basic/bbmd/h_bbmd.h"
#include "bacnet/datalink/datalink.h"
#include "bacnet/datalink/bip.h"
#include "bacnet/datalink/dlmstp.h"
#include "bacnet/datalink/mstp.h"
/* service handlers from bacnet-stack library */
#include "bacnet/basic/services.h"
#include "bacnet/basic/service/h_rp.h"
#include "bacnet/basic/service/h_rpm.h"
#include "bacnet/basic/service/h_wp.h"
#include "bacnet/basic/service/h_whois.h"
#include "bacnet/basic/service/h_iam.h"
#include "bacnet/basic/service/h_cov.h"
#include "bacnet/basic/service/s_whois.h"
#include "bacnet/npdu.h"
#include "bacnet/basic/npdu/h_npdu.h"
#include "bacnet/bacenum.h"
#include "ds18b20.h"

static const char *TAG = "bacnet";

int override_nvs_on_flash = 0;  /* Exported for AV/BV modules */

static void bacnet_register_with_bbmd(void);
static void bacnet_receive_task(void *pvParameters);
static void bacnet_mstp_receive_task(void *pvParameters);
static void bacnet_core_task(void *pvParameters);
static void bacnet_cov_task(void *pvParameters);
static void sen54_task(void *pvParameters);
static void bacnet_process_frame_event(const bacnet_event_t *evt);
static void bacnet_dispatcher_tick_100ms(void);
static void bacnet_dispatcher_tick_1s(void);
static void profiled_handler_read_property(uint8_t *service_request,
    uint16_t service_len,
    BACNET_ADDRESS *src,
    BACNET_CONFIRMED_SERVICE_DATA *service_data);
static void profiled_handler_write_property(uint8_t *service_request,
    uint16_t service_len,
    BACNET_ADDRESS *src,
    BACNET_CONFIRMED_SERVICE_DATA *service_data);

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
static SemaphoreHandle_t bacnet_datalink_mutex = NULL;
static volatile uint32_t mstp_pdu_count = 0;
static volatile uint32_t mstp_apdu_count = 0;
static volatile uint32_t mstp_rp_total = 0;
static volatile uint32_t mstp_wp_total = 0;
static float mstp_rp_last_value = 0.0f;
static uint64_t bacnet_core_last_fast_tick_us = 0;
static uint64_t bacnet_core_last_slow_tick_us = 0;

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

static bool wifi_connected_now(void)
{
    if (!USER_ENABLE_BACNET_IP) {
        return false;
    }

    wifi_ap_record_t ap_info = {0};
    return esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK;
}

static char datalink_bip[] = "bip";
static char datalink_mstp[] = "mstp";
static char *datalink_default = NULL;

static uint8_t mstp_rx_buffer[512];
static uint8_t mstp_tx_buffer[512];
static struct mstp_port_struct_t mstp_port;
static struct dlmstp_user_data_t mstp_user;
static struct dlmstp_rs485_driver mstp_rs485_driver = {
    .init = MSTP_RS485_Init,
    .send = MSTP_RS485_Send,
    .read = MSTP_RS485_Read,
    .transmitting = MSTP_RS485_Transmitting,
    .baud_rate = MSTP_RS485_Baud_Rate,
    .baud_rate_set = MSTP_RS485_Baud_Rate_Set,
    .silence_milliseconds = MSTP_RS485_Silence_Milliseconds,
    .silence_reset = MSTP_RS485_Silence_Reset
};

static void bacnet_datalink_lock(char *name)
{
    if (bacnet_datalink_mutex) {
        xSemaphoreTake(bacnet_datalink_mutex, portMAX_DELAY);
    }
#if BACNET_USE_DISPATCHER_CORE
    bacnet_coordinator_activate_link_name(name);
#else
    datalink_set(name);
#endif
}

static void bacnet_datalink_unlock(void)
{
    if (datalink_default) {
#if BACNET_USE_DISPATCHER_CORE
        bacnet_coordinator_activate_link_name(datalink_default);
#else
        datalink_set(datalink_default);
#endif
    }
    if (bacnet_datalink_mutex) {
        xSemaphoreGive(bacnet_datalink_mutex);
    }
}

static bool bacnet_mstp_init(void)
{
    MSTP_RS485_Init();

    memset(&mstp_port, 0, sizeof(mstp_port));
    memset(&mstp_user, 0, sizeof(mstp_user));

    mstp_user.RS485_Driver = &mstp_rs485_driver;
    mstp_port.UserData = &mstp_user;
    mstp_port.InputBuffer = mstp_rx_buffer;
    mstp_port.InputBufferSize = sizeof(mstp_rx_buffer);
    mstp_port.OutputBuffer = mstp_tx_buffer;
    mstp_port.OutputBufferSize = sizeof(mstp_tx_buffer);

    dlmstp_set_interface((const char *)&mstp_port);
    dlmstp_set_mac_address(USER_MSTP_MAC_ADDRESS);
    dlmstp_set_max_info_frames(USER_MSTP_MAX_INFO_FRAMES);
    dlmstp_set_max_master(USER_MSTP_MAX_MASTER);
    dlmstp_set_baud_rate(USER_MSTP_BAUD_RATE);
    dlmstp_slave_mode_enabled_set(false);

    return dlmstp_init((char *)&mstp_port);
}

/* BACnet receive task - processes incoming BACnet messages */
static void bacnet_receive_task(void *pvParameters)
{
    (void)pvParameters;
    BACNET_ADDRESS src = {0};
    static uint8_t rx_buffer[600];  /* Smaller buffer in DRAM */
    uint16_t pdu_len = 0;

    ESP_LOGI(TAG, "BACnet receive task started");

    while (1) {
        /* Poll for incoming BACnet messages */
        memset(&src, 0, sizeof(src));
        pdu_len = bip_receive(&src, rx_buffer, sizeof(rx_buffer), 100);
        if (pdu_len > 0) {
            bacnet_event_t evt = {0};
            evt.type = BACNET_EVENT_RX_FRAME_BIP;
            evt.link_id = BACNET_EVENT_LINK_BIP;
            evt.length = pdu_len;
            evt.timestamp_us = (uint64_t)esp_timer_get_time();
            evt.src = src;
            if (evt.length > BACNET_EVENT_FRAME_MAX) {
                evt.length = BACNET_EVENT_FRAME_MAX;
            }
            
            /* CRITICAL: Deep copy frame data into self-contained event struct.
               This ensures the event owns its data and does not depend on
               the rx_buffer's lifetime (which may be reused by the next frame). */
            memcpy(evt.data.frame, rx_buffer, evt.length);
            
            /* Sanity check: Verify frame data was copied, not pointed-to.
               The event struct must be completely self-contained. */
            if (evt.length > 0 && evt.data.frame[0] != rx_buffer[0]) {
                ESP_LOGW(TAG, "BIP frame data verification failed: evt.data.frame[0]=%u rx_buffer[0]=%u",
                    evt.data.frame[0], rx_buffer[0]);
            }
            
            if (!bacnet_event_bus_enqueue(&evt, 0)) {
                ESP_LOGD(TAG, "BIP frame enqueue dropped (queue full)");
            }

#if !BACNET_USE_DISPATCHER_CORE
            stack_profile_sample(STACK_EVT_BACNET_IP_RX);
            /* Save original source from UDP socket before NPDU decode modifies it */
            BACNET_ADDRESS orig_src = src;
            BACNET_ADDRESS dest = {0};
            BACNET_NPDU_DATA npdu_data = {0};
            int apdu_offset = bacnet_npdu_decode(
                rx_buffer, pdu_len, &dest, &src, &npdu_data);
            /* If NPDU didn't have source routing info, restore from UDP socket */
            if (src.len == 0) {
                src = orig_src;
            }
            if (apdu_offset > 0 && apdu_offset < (int)pdu_len) {
                bacnet_datalink_lock(datalink_bip);
                apdu_handler(&src, &rx_buffer[apdu_offset], pdu_len - apdu_offset);
                bacnet_datalink_unlock();
                stack_profile_sample(STACK_EVT_BACNET_IP_RX);
            }
#endif
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* BACnet MS/TP receive task - processes incoming MS/TP frames */
static void bacnet_mstp_receive_task(void *pvParameters)
{
    (void)pvParameters;
    BACNET_ADDRESS src = {0};
    static uint8_t rx_buffer[600];
    uint16_t pdu_len = 0;

    ESP_LOGI(TAG, "BACnet MS/TP receive task started");

    while (1) {
        memset(&src, 0, sizeof(src));
        pdu_len = dlmstp_receive(&src, rx_buffer, sizeof(rx_buffer), 0);
        if (pdu_len > 0) {
            bacnet_event_t evt = {0};
            evt.type = BACNET_EVENT_RX_FRAME_MSTP;
            evt.link_id = BACNET_EVENT_LINK_MSTP;
            evt.length = pdu_len;
            evt.timestamp_us = (uint64_t)esp_timer_get_time();
            evt.src = src;
            if (evt.length > BACNET_EVENT_FRAME_MAX) {
                evt.length = BACNET_EVENT_FRAME_MAX;
            }
            
            /* CRITICAL: Deep copy frame data into self-contained event struct.
               This ensures the event owns its data and does not depend on
               the rx_buffer's lifetime (which may be reused by the next frame). */
            memcpy(evt.data.frame, rx_buffer, evt.length);
            
            /* Sanity check: Verify frame data was copied, not pointed-to.
               The event struct must be completely self-contained. */
            if (evt.length > 0 && evt.data.frame[0] != rx_buffer[0]) {
                ESP_LOGW(TAG, "MSTP frame data verification failed: evt.data.frame[0]=%u rx_buffer[0]=%u",
                    evt.data.frame[0], rx_buffer[0]);
            }
            
            if (!bacnet_event_bus_enqueue(&evt, 0)) {
                ESP_LOGD(TAG, "MSTP frame enqueue dropped (queue full)");
            }

#if !BACNET_USE_DISPATCHER_CORE
            stack_profile_sample(STACK_EVT_MSTP_RX);
            mstp_pdu_count++;
            BACNET_ADDRESS dest = {0};
            BACNET_NPDU_DATA npdu_data = {0};
            int apdu_offset = bacnet_npdu_decode(
                rx_buffer, pdu_len, &dest, &src, &npdu_data);
            if (apdu_offset > 0 && apdu_offset < (int)pdu_len) {
                mstp_apdu_count++;
                if ((apdu_offset + 4) <= (int)pdu_len) {
                    uint8_t pdu_type = rx_buffer[apdu_offset] & 0xF0;
                    uint8_t service_choice = rx_buffer[apdu_offset + 3];
                    if (pdu_type == PDU_TYPE_CONFIRMED_SERVICE_REQUEST &&
                        service_choice == SERVICE_CONFIRMED_READ_PROPERTY) {
                        mstp_rp_total++;
                        mstp_rp_last_value = Analog_Value_Present_Value(1);
                    } else if (pdu_type == PDU_TYPE_CONFIRMED_SERVICE_REQUEST &&
                        service_choice == SERVICE_CONFIRMED_WRITE_PROPERTY) {
                        mstp_wp_total++;
                    }
                }
                bacnet_datalink_lock(datalink_mstp);
                apdu_handler(&src, &rx_buffer[apdu_offset], pdu_len - apdu_offset);
                bacnet_datalink_unlock();
                stack_profile_sample(STACK_EVT_MSTP_RX);
            } else {
                ESP_LOGW(TAG, "MS/TP RX frame decode failed: len=%u apdu_offset=%d src.len=%u src.mac=%u",
                    (unsigned)pdu_len, apdu_offset, (unsigned)src.len,
                    (unsigned)(src.len ? src.mac[0] : 0));
            }
#endif
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/* Application entry: init Wi‑Fi and BACnet */
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();

    bacnet_datalink_mutex = xSemaphoreCreateMutex();
    if (!bacnet_datalink_mutex) {
        ESP_LOGE(TAG, "Failed to create BACnet datalink mutex");
    }

    stack_profile_init();

    bacnet_coordinator_init();
    if (!bacnet_event_bus_init(0)) {  /* Use safe default queue size (16 items) */
        ESP_LOGE(TAG, "Failed to initialize BACnet event bus");
    }
    
    /* Give the scheduler time to stabilize queues before tasks start using them */
    vTaskDelay(pdMS_TO_TICKS(50));
    
    /* If OVERRIDE_NVS_ON_FLASH is set, always erase NVS to reset to code defaults */
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

    if (USER_ENABLE_BACNET_IP) {
        /* Initialize network stack (must be done before WiFi init) */
        esp_netif_init();
        esp_event_loop_create_default();

        wifi_init_sta();

        ESP_LOGI(TAG, "Initializing BACnet stack (B/IP)");
#if BACNET_USE_DISPATCHER_CORE
        bacnet_coordinator_activate_link(BACNET_LINK_BIP);
#else
        datalink_set(datalink_bip);
#endif
        if (!datalink_init(NULL)) {
            ESP_LOGE(TAG, "Failed to initialize BACnet datalink");
            return;
        }
        bacnet_coordinator_set_link_ready(BACNET_LINK_BIP, true);
        bacnet_coordinator_select_active_link();
        bacnet_coordinator_set_active_preference(BACNET_LINK_BIP);

        bacnet_register_with_bbmd();
    }

    if (USER_ENABLE_BACNET_MSTP) {
        ESP_LOGI(TAG, "Initializing BACnet MS/TP");
        if (!bacnet_mstp_init()) {
            ESP_LOGE(TAG, "Failed to initialize BACnet MS/TP datalink");
        } else {
#if BACNET_USE_DISPATCHER_CORE
            bacnet_coordinator_activate_link(BACNET_LINK_MSTP);
#else
            datalink_set(datalink_mstp);
#endif
            if (!datalink_init((char *)&mstp_port)) {
                ESP_LOGE(TAG, "Failed to initialize BACnet MS/TP datalink interface");
            } else {
                bacnet_coordinator_set_link_ready(BACNET_LINK_MSTP, true);
                bacnet_coordinator_select_active_link();
            }
        }
    }

    if (USER_ENABLE_BACNET_IP) {
        datalink_default = datalink_bip;
    } else if (USER_ENABLE_BACNET_MSTP) {
        datalink_default = datalink_mstp;
    }
    if (datalink_default) {
#if BACNET_USE_DISPATCHER_CORE
        bacnet_coordinator_activate_link_name(datalink_default);
#else
        datalink_set(datalink_default);
#endif
    }
    if (datalink_default == datalink_mstp) {
        bacnet_coordinator_set_active_preference(BACNET_LINK_MSTP);
    }

    Device_Init(NULL);
    Device_Set_Object_Instance_Number(USER_BACNET_DEVICE_INSTANCE);
    Device_Set_Vendor_Identifier(260);
    Device_Object_Name_ANSI_Init(USER_BACNET_DEVICE_NAME);

    /* Register service handlers - using bacnet-stack library handlers */
    ESP_LOGI(TAG, "Registering BACnet service handlers");
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_I_AM, handler_i_am_add);
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_WHO_IS, handler_who_is);
    apdu_set_unrecognized_service_handler_handler(handler_unrecognized_service);
    /* Read Property - REQUIRED for BACnet devices */
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_READ_PROPERTY, profiled_handler_read_property);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_READ_PROP_MULTIPLE, handler_read_property_multiple);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_WRITE_PROPERTY, profiled_handler_write_property);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_SUBSCRIBE_COV, handler_cov_subscribe);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_SUBSCRIBE_COV_PROPERTY, handler_cov_subscribe_property);

    /* Initialize COV subscription list */
    handler_cov_init();

    /* Create BACnet objects (AI, AV, BI, BV, BO) */
    bacnet_create_analog_inputs();
    bacnet_create_analog_values();
    bacnet_create_binary_inputs();
    bacnet_create_binary_values();
    bacnet_create_binary_outputs_with_gpio_sync();  /* Create BO with GPIO sync task */

    ESP_LOGI(TAG, "Broadcasting I-Am");
    if (USER_ENABLE_BACNET_IP) {
        bacnet_datalink_lock(datalink_bip);
        Send_I_Am(Handler_Transmit_Buffer);
        bacnet_datalink_unlock();
    }
    if (USER_ENABLE_BACNET_MSTP) {
        bacnet_datalink_lock(datalink_mstp);
        Send_I_Am(Handler_Transmit_Buffer);
        bacnet_datalink_unlock();
    }

    /* Initialize display */
    ESP_LOGI(TAG, "Initializing display");
    display_init();

    /* Start BACnet receive task to handle incoming messages */
    if (USER_ENABLE_BACNET_IP) {
        if (xTaskCreate(bacnet_receive_task, "bacnet_rx", 16384, NULL, 5, &bacnet_rx_task_handle) != pdPASS) {
            ESP_LOGE(TAG, "Failed to create bacnet_rx task");
            bacnet_rx_task_handle = NULL;
        }
    }
    if (USER_ENABLE_BACNET_MSTP) {
        if (xTaskCreate(bacnet_mstp_receive_task, "bacnet_mstp_rx", 12288, NULL, 5, &bacnet_mstp_rx_task_handle) != pdPASS) {
            ESP_LOGE(TAG, "Failed to create bacnet_mstp_rx task");
            bacnet_mstp_rx_task_handle = NULL;
        }
    }
#if !BACNET_USE_DISPATCHER_CORE
    if (xTaskCreate(bacnet_cov_task, "bacnet_cov", 24576, NULL, 4, &bacnet_cov_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create bacnet_cov task");
    }
#endif
#if BACNET_USE_DISPATCHER_CORE
    /* Increased from 12288 to 20480 bytes to accommodate:
       - bacnet_event_t (~650 byte stack allocation)
       - NPDU decode processing
       - APDU handler execution with nested function calls */
    if (xTaskCreate(bacnet_core_task, "bacnet_core", 20480, NULL, 2, &bacnet_core_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create bacnet_core task");
        bacnet_core_task_handle = NULL;
    }
#endif
    if (xTaskCreate(sen54_task, "sen54", 4096, NULL, 3, &sen54_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sen54 task");
        sen54_task_handle = NULL;
    }

    if (USER_ENABLE_BACNET_MSTP) {
        ESP_LOGI(TAG, "BACnet MS/TP ready");
    }

    /* Keep the task alive - maintenance + display updates */
    uint32_t display_tick = 0;
    uint32_t iam_tick = 0;
    uint32_t mstp_rx_tick = 0;
    uint32_t mstp_last_seen_pdu = 0;
    uint8_t mstp_alive_ticks = 0;
    uint32_t stack_report_tick = 0;
    while (1) {
#if !BACNET_USE_DISPATCHER_CORE
        if (USER_ENABLE_BACNET_IP) {
            bacnet_datalink_lock(datalink_bip);
            datalink_maintenance_timer(1);
            bacnet_datalink_unlock();
        }
#endif

        if (USER_ENABLE_BACNET_MSTP && ++iam_tick % 60 == 0) {
            bacnet_datalink_lock(datalink_mstp);
            Send_I_Am(Handler_Transmit_Buffer);
            bacnet_datalink_unlock();
        }

        if (USER_ENABLE_BACNET_MSTP && ++mstp_rx_tick % 30 == 0) {
            MSTP_RS485_Rx_Bytes_Get_Reset();
            MSTP_RS485_Preamble_Counts_Get_Reset(NULL, NULL);
            mstp_pdu_count = 0;
            mstp_apdu_count = 0;
            mstp_rp_total = 0;
            mstp_wp_total = 0;
        }

        stack_profile_sample(STACK_EVT_NORMAL);

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
            wifi_connected_now(),
            USER_ENABLE_BACNET_MSTP && (mstp_alive_ticks > 0));
        
        /* Update display every 2 seconds */
            float ai1_temp = Analog_Input_Present_Value(1);
            float ai2_humidity = Analog_Input_Present_Value(2);
            float ai3_voc = Analog_Input_Present_Value(3);
            float ai5_pm25 = Analog_Input_Present_Value(5);

            stack_profile_sample(STACK_EVT_DISPLAY_UPDATE);

            display_update_values(
                ai5_pm25,
                ai1_temp,
                ai2_humidity,
                ai3_voc);

stack_profile_sample(STACK_EVT_DISPLAY_UPDATE);

display_update_values(
    ai5_pm25,
    ai1_temp,
    ai2_humidity,
    ai3_voc);

stack_profile_sample(STACK_EVT_DISPLAY_UPDATE);
            stack_profile_sample(STACK_EVT_DISPLAY_UPDATE);
            display_update_values(
                                    ai5_pm25,
                                    ai1_temp,
                                    ai2_humidity,
                                    ai3_voc);
            stack_profile_sample(STACK_EVT_DISPLAY_UPDATE);
        }

        if (++stack_report_tick % 30 == 0) {
            stack_profile_log_report();
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }


/* COV task - handles COV timer and notifications */
static void bacnet_cov_task(void *pvParameters)
{
    (void)pvParameters;
    while (1) {
        char *active_datalink = datalink_default;
        if (!active_datalink) {
            if (USER_ENABLE_BACNET_IP) {
                active_datalink = datalink_bip;
            } else if (USER_ENABLE_BACNET_MSTP) {
                active_datalink = datalink_mstp;
            }
        }

        if (active_datalink) {
            bacnet_datalink_lock(active_datalink);
            handler_cov_timer_seconds(1);
            stack_profile_sample(STACK_EVT_COV_NOTIFICATION);
            handler_cov_task();
            stack_profile_sample(STACK_EVT_COV_NOTIFICATION);
            bacnet_datalink_unlock();
        } else {
            handler_cov_timer_seconds(1);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* Sensor task - reads SEN54 and DS18B20 data and writes to BACnet objects
 *
 * PERIPHERAL-TO-BACNET MAPPING:
 * - AI1 (instance 1): SEN54 Temperature (°C)
 * - AI2 (instance 2): SEN54 Relative Humidity (%RH)
 * - AI3 (instance 3): SEN54 VOC Index (dimensionless)
 * - AI4 (instance 4): SEN54 PM1.0 (ug/m3)
 * - AI5 (instance 5): SEN54 PM2.5 (ug/m3)
 * - AI6 (instance 6): SEN54 PM4.0 (ug/m3)
 * - AI7 (instance 7): SEN54 PM10 (ug/m3)
 * - AI8 (instance 8): DS18B20 Temperature (°C)
 */
static void sen54_task(void *pvParameters)
{
    (void)pvParameters;
    float ds18b20_temperature = 0.0f;
    sen54_data_t sensor_data;

    sen54_init();

    /* Wait for the sensor fan and particle chamber to stabilize */
    vTaskDelay(pdMS_TO_TICKS(5000));

    while (1) {
        /* BV1 written ACTIVE triggers a full SEN54 reset (I2C 0xD304) */
        if (Binary_Value_Present_Value(1) == BINARY_ACTIVE) {
            ESP_LOGI(TAG, "BV1 ACTIVE: sending SEN54 full reset");
            esp_err_t err = sen54_full_reset();
            ESP_LOGI(TAG, "SEN54 full reset %s", err == ESP_OK ? "OK" : "FAILED");
            Binary_Value_Present_Value_Set(1, BINARY_INACTIVE);
            continue;
        }

        stack_profile_sample(STACK_EVT_SEN54_READ);
        if (sen54_read(&sensor_data)) {
            Analog_Input_Present_Value_Set(1, sensor_data.temperature);
            Analog_Input_Present_Value_Set(2, sensor_data.humidity);
            Analog_Input_Present_Value_Set(3, sensor_data.voc_index);
            Analog_Input_Present_Value_Set(4, sensor_data.pm1_0);
            Analog_Input_Present_Value_Set(5, sensor_data.pm2_5);
            Analog_Input_Present_Value_Set(6, sensor_data.pm4_0);
            Analog_Input_Present_Value_Set(7, sensor_data.pm10);
            Analog_Input_Reliability_Set(1, RELIABILITY_NO_FAULT_DETECTED);
            Analog_Input_Reliability_Set(2, RELIABILITY_NO_FAULT_DETECTED);
            Analog_Input_Reliability_Set(3, RELIABILITY_NO_FAULT_DETECTED);
            Analog_Input_Reliability_Set(4, RELIABILITY_NO_FAULT_DETECTED);
            Analog_Input_Reliability_Set(5, RELIABILITY_NO_FAULT_DETECTED);
            Analog_Input_Reliability_Set(6, RELIABILITY_NO_FAULT_DETECTED);
            Analog_Input_Reliability_Set(7, RELIABILITY_NO_FAULT_DETECTED);
            stack_profile_sample(STACK_EVT_SEN54_READ);
        } else {
            Analog_Input_Reliability_Set(1, RELIABILITY_UNRELIABLE_OTHER);
            Analog_Input_Reliability_Set(2, RELIABILITY_UNRELIABLE_OTHER);
            Analog_Input_Reliability_Set(3, RELIABILITY_UNRELIABLE_OTHER);
            Analog_Input_Reliability_Set(4, RELIABILITY_UNRELIABLE_OTHER);
            Analog_Input_Reliability_Set(5, RELIABILITY_UNRELIABLE_OTHER);
            Analog_Input_Reliability_Set(6, RELIABILITY_UNRELIABLE_OTHER);
            Analog_Input_Reliability_Set(7, RELIABILITY_UNRELIABLE_OTHER);
            stack_profile_sample(STACK_EVT_SEN54_READ);
        }

        if (ds18b20_read_temperature(&ds18b20_temperature)) {
            Analog_Input_Present_Value_Set(8, ds18b20_temperature);
            Analog_Input_Reliability_Set(8, RELIABILITY_NO_FAULT_DETECTED);
        } else {
            Analog_Input_Reliability_Set(8, RELIABILITY_UNRELIABLE_OTHER);
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void bacnet_register_with_bbmd(void)
{
    BACNET_IP_ADDRESS bbmd_addr = { { USER_BBMD_IP_OCTET_1, USER_BBMD_IP_OCTET_2,
                                     USER_BBMD_IP_OCTET_3, USER_BBMD_IP_OCTET_4 },
                                    USER_BBMD_PORT };
    int result = bvlc_register_with_bbmd(&bbmd_addr, USER_BBMD_TTL_SECONDS);
    ESP_LOGI(TAG, "BBMD register result: %d", result);
}

static void profiled_handler_read_property(
    uint8_t *service_request,
    uint16_t service_len,
    BACNET_ADDRESS *src,
    BACNET_CONFIRMED_SERVICE_DATA *service_data)
{
    stack_profile_sample(STACK_EVT_READ_PROPERTY);
    handler_read_property(service_request, service_len, src, service_data);
    stack_profile_sample(STACK_EVT_READ_PROPERTY);
}

static void profiled_handler_write_property(
    uint8_t *service_request,
    uint16_t service_len,
    BACNET_ADDRESS *src,
    BACNET_CONFIRMED_SERVICE_DATA *service_data)
{
    stack_profile_sample(STACK_EVT_WRITE_PROPERTY);
    handler_write_property(service_request, service_len, src, service_data);
    stack_profile_sample(STACK_EVT_WRITE_PROPERTY);
}

static void bacnet_process_frame_event(const bacnet_event_t *evt)
{
#if BACNET_USE_DISPATCHER_CORE
    if ((evt == NULL) || (evt->length == 0)) {
        return;
    }

    if (evt->link_id == BACNET_EVENT_LINK_BIP) {
        BACNET_ADDRESS src = evt->src;
        BACNET_ADDRESS orig_src = src;
        BACNET_ADDRESS dest = {0};
        BACNET_NPDU_DATA npdu_data = {0};
        int apdu_offset = bacnet_npdu_decode(
            (uint8_t *)evt->data.frame, evt->length, &dest, &src, &npdu_data);

        if (src.len == 0) {
            src = orig_src;
        }

        if (apdu_offset > 0 && apdu_offset < (int)evt->length) {
            bacnet_datalink_lock(datalink_bip);
            apdu_handler(&src, (uint8_t *)&evt->data.frame[apdu_offset], evt->length - apdu_offset);
            bacnet_datalink_unlock();
        }
    } else if (evt->link_id == BACNET_EVENT_LINK_MSTP) {
        BACNET_ADDRESS src = evt->src;

        /* Preserve MS/TP diagnostic counters without changing protocol flow. */
        BACNET_ADDRESS dest = {0};
        BACNET_NPDU_DATA npdu_data = {0};
        int apdu_offset = bacnet_npdu_decode(
            (uint8_t *)evt->data.frame, evt->length, &dest, &src, &npdu_data);
        if (apdu_offset > 0 && apdu_offset < (int)evt->length) {
            mstp_apdu_count++;
            if ((apdu_offset + 4) <= (int)evt->length) {
                uint8_t pdu_type = evt->data.frame[apdu_offset] & 0xF0;
                uint8_t service_choice = evt->data.frame[apdu_offset + 3];
                if (pdu_type == PDU_TYPE_CONFIRMED_SERVICE_REQUEST &&
                    service_choice == SERVICE_CONFIRMED_READ_PROPERTY) {
                    mstp_rp_total++;
                    mstp_rp_last_value = Analog_Value_Present_Value(1);
                } else if (pdu_type == PDU_TYPE_CONFIRMED_SERVICE_REQUEST &&
                    service_choice == SERVICE_CONFIRMED_WRITE_PROPERTY) {
                    mstp_wp_total++;
                }
            }

            bacnet_datalink_lock(datalink_mstp);
            apdu_handler(&src, (uint8_t *)&evt->data.frame[apdu_offset], evt->length - apdu_offset);
            bacnet_datalink_unlock();
        } else {
            ESP_LOGW(TAG, "MS/TP RX frame decode failed: len=%u apdu_offset=%d src.len=%u src.mac=%u",
                (unsigned)evt->length, apdu_offset, (unsigned)src.len,
                (unsigned)(src.len ? src.mac[0] : 0));
        }
    }
#else
    (void)evt;
#endif
}

/* BO1 GPIO sync: replaces bo1_gpio_sync_task */
static uint8_t dispatcher_bo1_last_state = BINARY_INACTIVE;

/* SEN54 dispatcher state */
static bool dispatcher_sen54_initialized = false;
static int32_t dispatcher_sen54_init_tick_count = -5;  /* Wait 5 seconds before reading */
static uint32_t dispatcher_sen54_read_tick_count = 0;   /* Read every 2 seconds (2 ticks of 1s) */

static void bacnet_dispatcher_tick_100ms(void)
{
    tsm_timer_milliseconds(100);

    if (USER_ENABLE_BACNET_IP) {
        bacnet_datalink_lock(datalink_bip);
        stack_profile_sample(STACK_EVT_COV_NOTIFICATION);
        handler_cov_task();
        stack_profile_sample(STACK_EVT_COV_NOTIFICATION);
        bacnet_datalink_unlock();
    }

    if (USER_ENABLE_BACNET_MSTP) {
        bacnet_datalink_lock(datalink_mstp);
        stack_profile_sample(STACK_EVT_COV_NOTIFICATION);
        handler_cov_task();
        stack_profile_sample(STACK_EVT_COV_NOTIFICATION);
        bacnet_datalink_unlock();
    }

    /* BO1 GPIO sync: read present value every 100ms (replaces bo1_gpio_sync_task) */
    dispatcher_bo1_last_state = Binary_Output_Present_Value(USER_BO_INSTANCES[0]);
}

/* SEN54 dispatcher handler: replaces sen54_task periodic read (every 2 seconds) */
static void bacnet_dispatcher_sen54_handler(void)
{
    /* Initialize on first call */
    if (!dispatcher_sen54_initialized) {
        if (dispatcher_sen54_init_tick_count < 0) {
            dispatcher_sen54_init_tick_count++;
            /* After 5 seconds (5 iterations of 1s tick), init the sensor */
            if (dispatcher_sen54_init_tick_count == 0) {
                sen54_init();
                dispatcher_sen54_initialized = true;
            }
            return;  /* Skip reading until init complete */
        }
    }

    /* Check BV1 for reset command (every tick) */
    if (Binary_Value_Present_Value(1) == BINARY_ACTIVE) {
        ESP_LOGI(TAG, "BV1 ACTIVE: sending SEN54 full reset");
        esp_err_t err = sen54_full_reset();
        ESP_LOGI(TAG, "SEN54 full reset %s", err == ESP_OK ? "OK" : "FAILED");
        Binary_Value_Present_Value_Set(1, BINARY_INACTIVE);
        dispatcher_sen54_read_tick_count = 0;  /* Reset read timer after reset */
        return;
    }

    /* Perform sensor read every 2 seconds (i.e., every 2nd iteration of 1s tick) */
    dispatcher_sen54_read_tick_count++;
    if (dispatcher_sen54_read_tick_count >= 2) {
        dispatcher_sen54_read_tick_count = 0;
        float ds18b20_temperature = 0.0f;
        sen54_data_t sensor_data;

        stack_profile_sample(STACK_EVT_SEN54_READ);
        if (sen54_read(&sensor_data)) {
            Analog_Input_Present_Value_Set(1, sensor_data.temperature);
            Analog_Input_Present_Value_Set(2, sensor_data.humidity);
            Analog_Input_Present_Value_Set(3, sensor_data.voc_index);
            Analog_Input_Present_Value_Set(4, sensor_data.pm1_0);
            Analog_Input_Present_Value_Set(5, sensor_data.pm2_5);
            Analog_Input_Present_Value_Set(6, sensor_data.pm4_0);
            Analog_Input_Present_Value_Set(7, sensor_data.pm10);
            Analog_Input_Reliability_Set(1, RELIABILITY_NO_FAULT_DETECTED);
            Analog_Input_Reliability_Set(2, RELIABILITY_NO_FAULT_DETECTED);
            Analog_Input_Reliability_Set(3, RELIABILITY_NO_FAULT_DETECTED);
            Analog_Input_Reliability_Set(4, RELIABILITY_NO_FAULT_DETECTED);
            Analog_Input_Reliability_Set(5, RELIABILITY_NO_FAULT_DETECTED);
            Analog_Input_Reliability_Set(6, RELIABILITY_NO_FAULT_DETECTED);
            Analog_Input_Reliability_Set(7, RELIABILITY_NO_FAULT_DETECTED);
            stack_profile_sample(STACK_EVT_SEN54_READ);
        } else {
            Analog_Input_Reliability_Set(1, RELIABILITY_UNRELIABLE_OTHER);
            Analog_Input_Reliability_Set(2, RELIABILITY_UNRELIABLE_OTHER);
            Analog_Input_Reliability_Set(3, RELIABILITY_UNRELIABLE_OTHER);
            Analog_Input_Reliability_Set(4, RELIABILITY_UNRELIABLE_OTHER);
            Analog_Input_Reliability_Set(5, RELIABILITY_UNRELIABLE_OTHER);
            Analog_Input_Reliability_Set(6, RELIABILITY_UNRELIABLE_OTHER);
            Analog_Input_Reliability_Set(7, RELIABILITY_UNRELIABLE_OTHER);
            stack_profile_sample(STACK_EVT_SEN54_READ);
        }

        if (ds18b20_read_temperature(&ds18b20_temperature)) {
            Analog_Input_Present_Value_Set(8, ds18b20_temperature);
            Analog_Input_Reliability_Set(8, RELIABILITY_NO_FAULT_DETECTED);
        } else {
            Analog_Input_Reliability_Set(8, RELIABILITY_UNRELIABLE_OTHER);
        }
    }
}

static void bacnet_dispatcher_tick_1s(void)
{
    handler_cov_timer_seconds(1);

    if (USER_ENABLE_BACNET_IP) {
        bacnet_datalink_lock(datalink_bip);
        datalink_maintenance_timer(1);
        bacnet_datalink_unlock();
    }

    /* SEN54 periodic read handler (replaces sen54_task) */
    bacnet_dispatcher_sen54_handler();
}

static void bacnet_core_task(void *pvParameters)
{
    (void)pvParameters;
    /* Use static allocation instead of stack to save ~650 bytes of stack space.
       This is safe because bacnet_core_task is the only consumer of evt. */
    static bacnet_event_t evt = {0};

#if BACNET_USE_DISPATCHER_CORE
    bacnet_core_last_fast_tick_us = (uint64_t)esp_timer_get_time();
    bacnet_core_last_slow_tick_us = bacnet_core_last_fast_tick_us;
    ESP_LOGI(TAG, "bacnet_core_task started (dispatcher mode)");
#else
    ESP_LOGI(TAG, "bacnet_core_task started (shadow mode)");
#endif

    /* Allow system to stabilize before starting queue operations */
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ESP_LOGI(TAG, "bacnet_core_task entering main loop");

    while (1) {
        if (bacnet_event_bus_dequeue(&evt, pdMS_TO_TICKS(100))) {
#if BACNET_USE_DISPATCHER_CORE
            if ((evt.type == BACNET_EVENT_RX_FRAME_BIP) ||
                (evt.type == BACNET_EVENT_RX_FRAME_MSTP)) {
                /* Sanity check: Verify event structure is valid before processing.
                   This ensures frame data was properly copied and event is self-contained. */
                if (evt.length > 0 && evt.length <= BACNET_EVENT_FRAME_MAX) {
                    bacnet_process_frame_event(&evt);
                } else {
                    ESP_LOGW(TAG, "dispatcher: invalid event length %u (max %u)",
                        (unsigned)evt.length, BACNET_EVENT_FRAME_MAX);
                }
            }
#else
            ESP_LOGD(TAG, "bacnet_core_task received event: type=%d len=%u",
                (int)evt.type, (unsigned)evt.length);
#endif
        }

#if BACNET_USE_DISPATCHER_CORE
        uint64_t now_us = (uint64_t)esp_timer_get_time();
        if ((now_us - bacnet_core_last_fast_tick_us) >= 100000ULL) {
            bacnet_core_last_fast_tick_us += 100000ULL;
            bacnet_dispatcher_tick_100ms();
        }
        if ((now_us - bacnet_core_last_slow_tick_us) >= 1000000ULL) {
            bacnet_core_last_slow_tick_us += 1000000ULL;
            bacnet_dispatcher_tick_1s();
        }
#endif
    }
}