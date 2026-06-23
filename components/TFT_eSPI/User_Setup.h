#define USER_SETUP_INFO "ESP32-S3 ST7796S 320x480 SPI"

// Driver
#define ST7796_DRIVER

// Resolution (native; rotation 3 gives 480x320 landscape)
#define TFT_WIDTH  320
#define TFT_HEIGHT 480

// SPI Pins
#define TFT_MOSI 10
#define TFT_SCLK 9
#define TFT_MISO -1
#define TFT_CS   13
#define TFT_DC   12
#define TFT_RST  11

// Backlight (active-high)
#define TFT_BL   14
#define TFT_BACKLIGHT_ON HIGH

// SPI port selection: USE_HSPI_PORT forces SPI_PORT=3 → SPIClass(HSPI)=SPI3 @ 0x60025000.
// FSPI=0 is invalid (REG_SPI_BASE(0)=0 → crash). USE_HSPI_PORT avoids that entirely.
#define USE_HSPI_PORT

// SPI Frequency
#define SPI_FREQUENCY  26000000

// Colour order – no inversion (Adafruit working config ended with INVOFF)
#define TFT_RGB_ORDER TFT_BGR
#define TFT_INVERSION_OFF

// Fonts
#define LOAD_GLCD   // Font 1. Original Adafruit font, default GLCD
#define LOAD_FONT2  // Font 2. Small 16px high font
#define LOAD_FONT4  // Font 4. Medium 26px high font
#define LOAD_FONT6  // Font 6. Large 48px font
#define LOAD_FONT7  // Font 7. 7 segment 48px font
#define LOAD_FONT8  // Font 8. Large 75px font
#define LOAD_GFXFF  // FreeFonts. Include access to the 48 GFX free fonts
#define SMOOTH_FONT // Smooth font anti-aliasing

// Touch (disabled)
#define TOUCH_CS -1