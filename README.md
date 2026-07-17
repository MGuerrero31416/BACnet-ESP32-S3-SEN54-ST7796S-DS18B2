# ESP32-S3 BACnet/IP + MS/TP WiFi Display (SEN54) (DS18B2)

ESP32-S3 based BACnet controller with a TFT display and 36 BACnet objects: 16 Analog Values, 4 Binary Values, 8 Analog Inputs, 4 Binary Inputs, and 4 Binary Outputs. It includes SEN54 air-quality monitoring for PM2.5/PM1.0/PM4.0/PM10, temperature, humidity, and VOC index data.

The device supports WiFi (BACnet/IP) and BACnet MS/TP over RS485, and can be configured for dual-stack operation.

Additional BACnet objects can be added and mapped to ESP32 GPIO for analog and digital I/O.

## Features

- **BACnet/IP Protocol**: Full BACnet/IP stack implementation
- **BACnet MS/TP**: RS485 MS/TP support alongside BACnet/IP (dual stack capable)
- **Live Display**: Real-time monitoring of BACnet objects on a 320x480 TFT display
- **36 BACnet Objects**:
  - 16 Analog Values (AV1-16): read/write with COV and NVS persistence
  - 4 Binary Values (BV1-4): read/write with COV and NVS persistence
  - 8 Analog Inputs (AI1-8): sensor inputs with COV and NVS persistence
  - 4 Binary Inputs (BI1-4): binary states with COV and NVS persistence
  - 4 Binary Outputs (BO1-4): writable control outputs with COV and NVS persistence
- **Writable Metadata**: Object `Name` and `Description` are writable for AV/BV/AI/BI/BO
- **WiFi Connectivity**: ESP32 with built-in WiFi for BACnet/IP communication
- **Arduino Framework**: Leverages Arduino ecosystem for easy hardware control
- **Change of Value (COV)**: Implements BACnet COV notifications for efficient real-time updates
- **Persistent Storage**: Attribute values modifiable from BACnet supervisor are automatically saved to ESP32 non-volatile memory (NVS) for retention across power cycles
- **NVS Override**: When `USER_OVERRIDE_NVS_ON_FLASH=1`, NVS is erased on boot and all values reset to defaults
- **Centralized Configuration**: User settings are centralized in [main/User_Settings.c](main/User_Settings.c)
- **Air Quality Monitoring**: SEN54 sensor with PM2.5/PM1.0/PM4.0/PM10, temperature, humidity, and VOC index with BACnet Analog Input integration

## Photos
![SEN54](docs/images/SEN54.jpg)
![YABE](docs/images/YABE SEN54.png)

## Hardware Requirements

- **Microcontroller**: ESP32-S3
- **Display**: ST7796S SPI TFT (320x480 panel, runtime rotation 1)
- **Display Connections**:
  - MOSI (SDA): GPIO 10
  - SCLK (SCL): GPIO 9
  - CS: GPIO 13
  - DC: GPIO 12
  - RST: GPIO 11
  - BL (Backlight): GPIO 14
- **DS18B2 Temperature Sensor**:
  - MOSI (SDA): GPIO 18


## Hardware Components

### ST7796S TFT Display
- **Resolution**: 320x480 pixels
- **Interface**: SPI (4-wire)
- **Driver**: Adafruit ST7735 and ST7789 Library (using ST7796S driver)

### SEN54 Air Quality Sensor
- **Model**: Sensirion SEN54
- **Communication**: I2C (address 0x69, 100 kHz)
- **Connections**:
  - SDA → ESP32 GPIO4
  - SCL → ESP32 GPIO5
  - Power: 3.3V or 5V
  - GND: ESP32 GND
- **Measurements**:
  - PM1.0, PM2.5, PM4.0, PM10 (µg/m³)
  - Temperature (°C) → mapped to **Analog Input 1**
  - Relative Humidity (%RH) → mapped to **Analog Input 2**
  - VOC Index (1–500) → mapped to **Analog Input 3**
  - PM1.0 → mapped to **Analog Input 4**
  - PM2.5 → mapped to **Analog Input 5**
  - PM4.0 → mapped to **Analog Input 6**
  - PM10 → mapped to **Analog Input 7**
  - Temp (from DS18B2) → mapped to **Analog Input 8**
- **BACnet Mapping**: Sensor mapping is handled in [main/main.c](main/main.c) (dispatcher SEN54 handler) and written to AI objects.
- **Update Frequency**: 2-second intervals
- **Features**:
  - CRC-8 validation on all I2C responses (Sensirion polynomial 0x31)
  - Sensor disconnect detection with BACnet error indication (-1 value)
  - Thread-safe FreeRTOS mutex-protected data

### WiFi Connectivity
- Built-in ESP32 WiFi for BACnet/IP communication
- Configured via [main/User_Settings.c](main/User_Settings.c)
- Default values in [main/User_Settings.c](main/User_Settings.c) use placeholders (`YOUR_WIFI_SSID` / `YOUR_WIFI_PASSWORD`) and should be changed for your environment
- Static IP option in [main/User_Settings.c](main/User_Settings.c). Set `USER_WIFI_USE_STATIC_IP` to 1 or 0

### BACnet MS/TP (RS485)
- **Transceiver**: MAX485 or equivalent RS485 converter
- **UART**: UART2
- **Connections**: RS485 transceiver GPIO mapping is project-specific and configured in [main/mstp_rs485.c](main/mstp_rs485.c)
- **Baud Rate**: 38400 (default)
- **MS/TP Settings**: MAC 21, Max Master 127, Max Info Frames 80
- **Default State**: Disabled by default (`USER_ENABLE_BACNET_MSTP=false`) in [main/User_Settings.c](main/User_Settings.c)
- **Discovery**: Some controllers (e.g., NAE) require manual add on the MS/TP field bus

## GPIO Summary

| Pin     | Component   | Signal              | Definition |
|---------|-------------|---------------------|------------|
| GPIO 4  | SEN54       | SDA (I2C Data)      | [components/sen54/sen54.h](components/sen54/sen54.h)
| GPIO 5  | SEN54       | SCL (I2C Clock)     | [components/sen54/sen54.h](components/sen54/sen54.h)
| GPIO 9  | TFT Display | SCLK (SPI Clock)    | [components/TFT_eSPI/User_Setup.h](components/TFT_eSPI/User_Setup.h)
| GPIO 10 | TFT Display | MOSI SDA (SPI Data) | [components/TFT_eSPI/User_Setup.h](components/TFT_eSPI/User_Setup.h)
| GPIO 11 | TFT Display | RST (Reset)         | [components/TFT_eSPI/User_Setup.h](components/TFT_eSPI/User_Setup.h)
| GPIO 12 | TFT Display | DC (Data/Command)   | [components/TFT_eSPI/User_Setup.h](components/TFT_eSPI/User_Setup.h)
| GPIO 13 | TFT Display | CS (Chip Select)    | [components/TFT_eSPI/User_Setup.h](components/TFT_eSPI/User_Setup.h)
| GPIO 14 | TFT Display | BACKLIGHT           | [components/TFT_eSPI/User_Setup.h](components/TFT_eSPI/User_Setup.h)
| GPIO 18 | DS18B2      | Temperature         | 


## Build Requirements

- ESP-IDF v5.5.x
- Python 3.11+
- xtensa-esp-elf toolchain


```
### Display Driver Settings

Display runtime initialization is handled in [main/display.cpp](main/display.cpp), while TFT pin mapping and panel setup are defined in [components/TFT_eSPI/User_Setup.h](components/TFT_eSPI/User_Setup.h), including:

- `tft.init()`
- `tft.setRotation(1)`
- `#define TFT_MOSI 10`
- `#define TFT_SCLK 9`
- `#define TFT_CS 13`
- `#define TFT_DC 12`
- `#define TFT_RST 11`
- `#define TFT_BL 14`

### FreeRTOS Configuration

Arduino framework requires FreeRTOS tick rate of 1000Hz. This is set in [sdkconfig](sdkconfig):

```
CONFIG_FREERTOS_HZ=1000
```

### User Settings (Centralized Configuration)

Most user-configurable settings are centralized in [main/User_Settings.c](main/User_Settings.c) and declared in [main/User_Settings.h](main/User_Settings.h), including:

- WiFi SSID/password and static IP settings
- BACnet Device Instance and BBMD registration
- BACnet/IP and MS/TP enable flags (`USER_ENABLE_BACNET_IP`, `USER_ENABLE_BACNET_MSTP`)
- MS/TP parameters (MAC, baud rate, max master, max info frames)
- Default object names, descriptions, units, and initial values

### BACnet Object Configuration

- **Analog Values (AV1-16)**: Configure names, descriptions, units, and initial values in [main/User_Settings.c](main/User_Settings.c)

- **Binary Values (BV1-4)**: Configure names, descriptions, active/inactive text, and initial states in [main/User_Settings.c](main/User_Settings.c)

- **Analog Inputs (AI1-8)**: Configure names, descriptions, units, and COV increments in [main/User_Settings.c](main/User_Settings.c). Read-only inputs suitable for sensor integration.

- **Binary Inputs (BI1-4)**: Configure names, descriptions, active/inactive text in [main/User_Settings.c](main/User_Settings.c). Read-only binary states.

- **Binary Outputs (BO1-4)**: Configure names, descriptions, active/inactive text, and initial states in [main/User_Settings.c](main/User_Settings.c). Writable control outputs with priority support.

### Sensor Data Mapping

- **SEN54 Parameters**: Update the sensor-to-object mapping in [main/main.c](main/main.c) (SEN54 dispatcher handler). Current default mapping: AI1=Temperature, AI2=Humidity, AI3=VOC Index, AI4=PM1.0, AI5=PM2.5, AI6=PM4.0, AI7=PM10, AI8=DS18B20 Temperature.

## Architecture

### Components

- **[components/bacnet-stack](components/bacnet-stack)** - BACnet/IP stack (modified from bacnet-stack/bacnet-stack)
- **[components/sen54](components/sen54)** - Sensirion SEN54 I2C driver
- **[components/Adafruit_BusIO](components/Adafruit_BusIO)** - Adafruit BusIO support library
- **[components/Adafruit_GFX_Library](components/Adafruit_GFX_Library)** - Adafruit graphics primitives
- **[components/Adafruit_ST7735_and_ST7789_Library](components/Adafruit_ST7735_and_ST7789_Library)** - Adafruit ST77xx/ST7796S driver library
- **[main](main/)** - Application code
  - `main.c` - BACnet initialization and main loop
  - `analog_value.c/h` - Analog Value object creation and NVS persistence
  - `binary_value.c/h` - Binary Value object creation and NVS persistence
  - `analog_input.c/h` - Analog Input object creation and NVS persistence
  - `binary_input.c/h` - Binary Input object creation and NVS persistence
  - `binary_output.c/h` - Binary Output object creation and NVS persistence
  - `display.cpp` - TFT display driver
  - `wifi_helper.c` - WiFi configuration helpers

### Display Layout

The display renders four primary values. BV, BI, and BO objects are not shown on screen.

| Item | Panel | Display |
|------|-------|---------|
| AV1 | AQI — bottom left | PM2.5 (µg/m³), numeric (1 decimal) |
| AI1 | Mid — left | Temperature (°C), numeric (1 decimal) |
| AV3 | Mid — right | Relative Humidity (%RH), numeric (1 decimal) |
| AV4 | AQI — bottom right | VOC Index (1–500), numeric (1 decimal) |

## BACnet Integration

The device broadcasts its Device ID and manages BACnet objects that can be read/written by any BACnet/IP or BACnet MS/TP client (e.g., YABE, Tridium Niagara, Metasys).

### BACnet Objects Exposed

- **Device**: 55502 by default (configurable in [main/User_Settings.c](main/User_Settings.c))
- **Analog Values**: Instances 1 through 16
- **Binary Values**: Instance 1, 2, 3, 4
- **Analog Inputs**: Instances 1 through 8
- **Binary Inputs**: Instance 1, 2, 3, 4
- **Binary Outputs**: Instance 1, 2, 3, 4

## Modifications to bacnet-stack

This project uses the official [bacnet-stack](https://github.com/bacnet-stack/bacnet-stack) with the following project-specific modifications:

- **[components/bacnet-stack/](components/bacnet-stack/)** - Configured as ESP-IDF component
- Simplified for embedded systems (reduced features, optimized for ESP32)
- WiFi-based BACnet/IP instead of Ethernet

No separate change log file is maintained; refer to git history for a record of modifications.

## Development Notes

### Display Boundary Constants

The display code uses boundary constants for easy layout modification:

```c
#define DISP_X0    0
#define DISP_Y0    0
#define DISP_X1    479
#define DISP_Y1    319
#define DISP_WIDTH 480
#define DISP_HEIGHT 320
```

Position all elements relative to these constants to avoid hardcoding coordinates.

## Troubleshooting

### Display orientation or color issues
If display output looks mirrored, rotated, or has swapped colors, adjust ST7796S init parameters and rotation in [main/display.cpp](main/display.cpp) and recompile.

### WiFi connection fails
Check SSID/password in [main/User_Settings.c](main/User_Settings.c), then verify WiFi init/connection flow in [main/wifi_helper.c](main/wifi_helper.c).

### Linker errors with Arduino
Ensure `CONFIG_FREERTOS_HZ=1000` is set in [sdkconfig](sdkconfig) and rebuild with `idf.py fullclean && idf.py build`.


## References

- [BACnet Stack GitHub](https://github.com/bacnet-stack/bacnet-stack)
- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/en/stable/)
- [Arduino-ESP32 GitHub](https://github.com/espressif/arduino-esp32)
- [Adafruit ST7735 and ST7789 Library GitHub](https://github.com/adafruit/Adafruit-ST7735-Library)


