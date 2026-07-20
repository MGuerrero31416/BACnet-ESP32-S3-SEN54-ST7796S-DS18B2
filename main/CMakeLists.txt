idf_component_register(
    SRCS
        "main.c"

        "app/adafruit_io_service.c"
        "app/app_storage.c"
        "app/app_supervisor.c"
        "app/sen54_bacnet_config.c"
        "app/sensor_service.c"
        "app/stack_profiler.c"

        "bacnet/bacnet_app.c"
        "bacnet/bacnet_coordinator.c"
        "bacnet/bacnet_event_bus.c"

        "bacnet/objects/analog_input.c"
        "bacnet/objects/analog_value.c"
        "bacnet/objects/binary_input.c"
        "bacnet/objects/binary_output.c"
        "bacnet/objects/binary_value.c"

        "platform/mstp_rs485.c"
        "platform/wifi_helper.c"

        "ui/display.cpp"

        "User_Settings.c"

    INCLUDE_DIRS
        "."
        "app"
        "bacnet"
        "bacnet/objects"
        "platform"
        "ui"
        "ui/fonts"

    REQUIRES
        bacnet-stack
        TFT_eSPI
        espressif__arduino-esp32
        sen54
        ds18b20

    PRIV_REQUIRES
        spi_flash
        nvs_flash
        esp_event
        esp_wifi
        esp_netif
        driver
        esp_timer
        mqtt
        mbedtls
)

# Add linker flags for Arduino log wrapper compatibility
target_link_options(
    ${COMPONENT_LIB}
    INTERFACE
        "-Wl,--wrap=esp_log_write"
        "-Wl,--wrap=esp_log_writev"
)

# Force the linker to include Arduino log wrapper symbols
target_link_libraries(
    ${COMPONENT_LIB}
    INTERFACE
        "-Wl,--undefined=__wrap_esp_log_write"
        "-Wl,--undefined=__wrap_esp_log_writev"
)