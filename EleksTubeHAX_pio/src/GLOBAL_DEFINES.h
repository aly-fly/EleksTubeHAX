/*
 * Author: Aljaz Ogrin
 * Project: Alternative firmware for EleksTube IPS clock
 * Hardware: ESP32
 * File description: Global configuration for the complete project
 *   Should only need editing to add a new harware version of the clock
 *   User configuration is located in "_USER_DEFINES.h"
 */

#ifndef GLOBAL_DEFINES_H_
#define GLOBAL_DEFINES_H_

#include <stdint.h>
#include <Arduino.h>
#include "_USER_DEFINES.h" ///// User defines

#ifdef HARDWARE_PunkCyber_CLOCK
// everything else is the same, except digits are swapped from left to right
#define HARDWARE_Elekstube_CLOCK
#endif

// ************* Version Infomation  *************
#define DEVICE_NAME "IPS-clock"
#define FIRMWARE_VERSION "SmittyHalibut & aly-fly IPS clock v1.0 HA Edition victorvuelma & gamba69"
#define SAVED_CONFIG_NAMESPACE "configs"

// ************ WiFi advanced config *********************
#define ESP_WPS_MODE WPS_TYPE_PBC // use WPS push-button methode
#define ESP_MANUFACTURER "ESPRESSIF"
#define ESP_MODEL_NUMBER "ESP32"
#define ESP_MODEL_NAME "IPS clock"
#define CONFIG_ESP32_WIFI_NVS_ENABLED 1 // Force NVS usage for WiFi driver

// ************ MQTT config *********************
#define MQTT_RECONNECT_WAIT_SEC 30      // how long to wait between retries to connect to broker
#define MQTT_REPORT_STATUS_EVERY_SEC 15 // How often report status to MQTT Broker

// ************ Temperature config *********************
#define TEMPERATURE_READ_EVERY_SEC 60 // how often to read the temperature sensor (if present)

// ************ Backlight config *********************
#define DEFAULT_BL_RAINBOW_DURATION_SEC 8

// ************ Hardware definitions *********************

// Disable all warnings from the TFT_eSPI lib
#define DISABLE_ALL_LIBRARY_WARNINGS

// Common indexing scheme, used to identify the digit
#define NUM_DIGITS (6)
#ifdef HARDWARE_PunkCyber_CLOCK
#define SECONDS_ONES (5)
#define SECONDS_TENS (4)
#define MINUTES_ONES (3)
#define MINUTES_TENS (2)
#define HOURS_ONES (1)
#define HOURS_TENS (0)
#else
#define SECONDS_ONES (0)
#define SECONDS_TENS (1)
#define MINUTES_ONES (2)
#define MINUTES_TENS (3)
#define HOURS_ONES (4)
#define HOURS_TENS (5)
#endif

#define SECONDS_ONES_MAP (0x01 << SECONDS_ONES)
#define SECONDS_TENS_MAP (0x01 << SECONDS_TENS)
#define MINUTES_ONES_MAP (0x01 << MINUTES_ONES)
#define MINUTES_TENS_MAP (0x01 << MINUTES_TENS)
#define HOURS_ONES_MAP (0x01 << HOURS_ONES)
#define HOURS_TENS_MAP (0x01 << HOURS_TENS)

#ifdef HARDWARE_SI_HAI_CLOCK // SI HAI IPS Clock XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

// #define ONE_WIRE_BUS_PIN (xx)  // DS18B20 connected to GPIOxx; comment this line if sensor is not connected

// WS2812 (or compatible) LEDs on the back of the display modules.
#define BACKLIGHTS_PIN (32)

// Buttons, active low, externally pulled up (with actual resistors!)
#define BUTTON_LEFT_PIN (35)
#define BUTTON_MODE_PIN (34)
#define BUTTON_RIGHT_PIN (39)
#define BUTTON_POWER_PIN (36)

// 3-wire to DS1302 RTC
#define DS1302_SCLK (33)
#define DS1302_IO (25)
#define DS1302_CE (26)

// Chip Select shift register, to select the display
#define CSSR_DATA_PIN (4)
#define CSSR_CLOCK_PIN (22)
#define CSSR_LATCH_PIN (21)

// SPI to displays
// Backlight for all TFT displays are powered through a MOSFET so they can all be turned off.
// Active HIGH.
#define TFT_ENABLE_PIN (2)

// configure library \TFT_eSPI\User_Setup.h
// ST7789 135 x 240 display with no chip select line
#define ST7789_DRIVER // Configure all registers
#define TFT_WIDTH 135
#define TFT_HEIGHT 240
#define CGRAM_OFFSET // Library will add offsets required
#define TFT_SDA_READ // Read and write on the MOSI/SDA pin, no separate MISO pin
#define TFT_MOSI 19
#define TFT_SCLK 18
// #define TFT_CS    -1 // Not connected
#define TFT_DC 16  // Data Command, aka Register Select or RS
#define TFT_RST 23 // Connect reset to ensure display initialises

// #define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font needs ~1820 bytes in FLASH
#define LOAD_FONT2 // Font 2. Small 16 pixel high font, needs ~3534 bytes in FLASH, 96 characters
#define LOAD_FONT4 // Font 4. Medium 26 pixel high font, needs ~5848 bytes in FLASH, 96 characters
// #define LOAD_FONT6  // Font 6. Large 48 pixel font, needs ~2666 bytes in FLASH, only characters 1234567890:-.apm
// #define LOAD_FONT7  // Font 7. 7 segment 48 pixel font, needs ~2438 bytes in FLASH, only characters 1234567890:.
// #define LOAD_FONT8  // Font 8. Large 75 pixel font needs ~3256 bytes in FLASH, only characters 1234567890:-.
// #define LOAD_FONT8N // Font 8. Alternative to Font 8 above, slightly narrower, so 3 digits fit a 160 pixel TFT
// #define LOAD_GFXFF  // FreeFonts. Include access to the 48 Adafruit_GFX free fonts FF1 to FF48 and custom fonts

#define SMOOTH_FONT // MUST REMAIN ACTIVE OTHERWISE BUTTON CONFIG IS CORRUPTED for some reason....
// #define SPI_FREQUENCY  27000000
#define SPI_FREQUENCY 40000000
/*
 * To make the Library not over-write all this:
 */
#define USER_SETUP_LOADED
#endif // SI HAI IPS Clock XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

#ifdef HARDWARE_NovelLife_SE_CLOCK // NovelLife_SE Clone XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

// WS2812 (or compatible) LEDs on the back of the display modules.
#define BACKLIGHTS_PIN (GPIO_NUM_12)

// No Buttons on SE verion!!!
// Set to pins, which should always be HIGH!
#define BUTTON_LEFT_PIN (GPIO_NUM_3)
#define BUTTON_MODE_PIN (GPIO_NUM_3)
#define BUTTON_RIGHT_PIN (GPIO_NUM_3)
#define BUTTON_POWER_PIN (GPIO_NUM_3)

// Pins ADPS interupt
#define GESTURE_SENSOR_INPUT_PIN (GPIO_NUM_5) // -> INTERRUPT

// I2C to DS3231 RTC.
#define RTC_SCL_PIN (22)
#define RTC_SDA_PIN (21)

// Chip Select shift register, to select the display
#define CSSR_DATA_PIN (14)
#define CSSR_CLOCK_PIN (13) // SHcp changed from IO16 in original Elekstube
#define CSSR_LATCH_PIN (15) // STcp was IO17 in original Elekstube

// SPI to displays
// DEFINED IN User_Setup.h
// Look for: TFT_MOSI, TFT_SCLK, TFT_CS, TFT_DC, and TFT_RST

// Power for all TFT displays are grounded through a MOSFET so they can all be turned off.
// Active HIGH.
#define TFT_ENABLE_PIN (4) /// Was 27 on elekstube

// configure library \TFT_eSPI\User_Setup.h
// ST7789 135 x 240 display with no chip select line
#define ST7789_DRIVER // Configure all registers
#define TFT_WIDTH 135
#define TFT_HEIGHT 240
#define CGRAM_OFFSET // Library will add offsets required
#define TFT_SDA_READ // Read and write on the MOSI/SDA pin, no separate MISO pin
#define TFT_MOSI 23
#define TFT_SCLK 18
// #define TFT_CS    -1 // Not connected
#define TFT_DC 25  // Data Command, aka Register Select or RS
#define TFT_RST 26 // Connect reset to ensure display initialises

// #define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font needs ~1820 bytes in FLASH
#define LOAD_FONT2 // Font 2. Small 16 pixel high font, needs ~3534 bytes in FLASH, 96 characters
#define LOAD_FONT4 // Font 4. Medium 26 pixel high font, needs ~5848 bytes in FLASH, 96 characters
// #define LOAD_FONT6  // Font 6. Large 48 pixel font, needs ~2666 bytes in FLASH, only characters 1234567890:-.apm
// #define LOAD_FONT7  // Font 7. 7 segment 48 pixel font, needs ~2438 bytes in FLASH, only characters 1234567890:.
// #define LOAD_FONT8  // Font 8. Large 75 pixel font needs ~3256 bytes in FLASH, only characters 1234567890:-.
// #define LOAD_FONT8N // Font 8. Alternative to Font 8 above, slightly narrower, so 3 digits fit a 160 pixel TFT
// #define LOAD_GFXFF  // FreeFonts. Include access to the 48 Adafruit_GFX free fonts FF1 to FF48 and custom fonts

#define SMOOTH_FONT
// #define SPI_FREQUENCY  27000000
#define SPI_FREQUENCY 40000000
/*
 * To make the Library not over-write all this:
 */
#define USER_SETUP_LOADED
#endif // NovelLife_SE Clone XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

#ifdef HARDWARE_Elekstube_CLOCK // original EleksTube IPS clock XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

// WS2812 (or compatible) LEDs on the back of the display modules.
#define BACKLIGHTS_PIN (12)

// Buttons, active low, externally pulled up (with actual resistors!)
#define BUTTON_LEFT_PIN (33)
#define BUTTON_MODE_PIN (32)
#define BUTTON_RIGHT_PIN (35)
#define BUTTON_POWER_PIN (34)

// I2C to DS3231 RTC.
#define RTC_SCL_PIN (22)
#define RTC_SDA_PIN (21)

// Chip Select shift register, to select the display
#define CSSR_DATA_PIN (14)
#define CSSR_CLOCK_PIN (16)
#define CSSR_LATCH_PIN (17)

// SPI to displays
// DEFINED IN User_Setup.h
// Look for: TFT_MOSI, TFT_SCLK, TFT_CS, TFT_DC, and TFT_RST

// Power for all TFT displays are grounded through a MOSFET so they can all be turned off.
// Active HIGH.
#define TFT_ENABLE_PIN (27)

// configure library \TFT_eSPI\User_Setup.h
// ST7789 135 x 240 display with no chip select line
#define ST7789_DRIVER // Configure all registers
#define TFT_WIDTH 135
#define TFT_HEIGHT 240
#define CGRAM_OFFSET // Library will add offsets required
#define TFT_SDA_READ // Read and write on the MOSI/SDA pin, no separate MISO pin
#define TFT_MOSI 23
#define TFT_SCLK 18
// #define TFT_CS    -1 // Not connected
#define TFT_DC 25  // Data Command, aka Register Select or RS
#define TFT_RST 26 // Connect reset to ensure display initialises

// #define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font needs ~1820 bytes in FLASH
#define LOAD_FONT2 // Font 2. Small 16 pixel high font, needs ~3534 bytes in FLASH, 96 characters
#define LOAD_FONT4 // Font 4. Medium 26 pixel high font, needs ~5848 bytes in FLASH, 96 characters
// #define LOAD_FONT6  // Font 6. Large 48 pixel font, needs ~2666 bytes in FLASH, only characters 1234567890:-.apm
// #define LOAD_FONT7  // Font 7. 7 segment 48 pixel font, needs ~2438 bytes in FLASH, only characters 1234567890:.
// #define LOAD_FONT8  // Font 8. Large 75 pixel font needs ~3256 bytes in FLASH, only characters 1234567890:-.
// #define LOAD_FONT8N // Font 8. Alternative to Font 8 above, slightly narrower, so 3 digits fit a 160 pixel TFT
// #define LOAD_GFXFF  // FreeFonts. Include access to the 48 Adafruit_GFX free fonts FF1 to FF48 and custom fonts

#define SMOOTH_FONT // MUST REMAIN ACTIVE OTHERWISE BUTTON CONFIG IS CORRUPTED for some reason....
// #define SPI_FREQUENCY  27000000
#define SPI_FREQUENCY 40000000
/*
 * To make the Library not over-write all this:
 */
#define USER_SETUP_LOADED
#endif // original EleksTube IPS clock XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

#ifdef HARDWARE_Elekstube_CLOCK_Gen2 // original EleksTube IPS clock Gen2 XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

// WS2812 (or compatible) LEDs on the back of the display modules.
#define BACKLIGHTS_PIN (12)

// Buttons, active low, externally pulled up (with actual resistors!)
#define BUTTON_LEFT_PIN (33)
#define BUTTON_MODE_PIN (32)
#define BUTTON_RIGHT_PIN (35)
#define BUTTON_POWER_PIN (34)

// I2C to DS3231 RTC.
#define RTC_SCL_PIN (22)
#define RTC_SDA_PIN (21)

// Chip Select shift register, to select the display
#define CSSR_DATA_PIN (14)
#define CSSR_CLOCK_PIN (9)
#define CSSR_LATCH_PIN (10)

// SPI to displays
// DEFINED IN User_Setup.h
// Look for: TFT_MOSI, TFT_SCLK, TFT_CS, TFT_DC, and TFT_RST

// Power for all TFT displays are grounded through a MOSFET so they can all be turned off.
// Active HIGH.
#define TFT_ENABLE_PIN (27)

// configure library \TFT_eSPI\User_Setup.h
// ST7789 135 x 240 display with no chip select line
#define ST7789_DRIVER // Configure all registers
#define TFT_WIDTH 135
#define TFT_HEIGHT 240
#define CGRAM_OFFSET // Library will add offsets required
#define TFT_SDA_READ // Read and write on the MOSI/SDA pin, no separate MISO pin
#define TFT_MOSI 23
#define TFT_SCLK 18
// #define TFT_CS    -1 // Not connected
#define TFT_DC 25  // Data Command, aka Register Select or RS
#define TFT_RST 26 // Connect reset to ensure display initialises

// #define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font needs ~1820 bytes in FLASH
#define LOAD_FONT2 // Font 2. Small 16 pixel high font, needs ~3534 bytes in FLASH, 96 characters
#define LOAD_FONT4 // Font 4. Medium 26 pixel high font, needs ~5848 bytes in FLASH, 96 characters
// #define LOAD_FONT6  // Font 6. Large 48 pixel font, needs ~2666 bytes in FLASH, only characters 1234567890:-.apm
// #define LOAD_FONT7  // Font 7. 7 segment 48 pixel font, needs ~2438 bytes in FLASH, only characters 1234567890:.
// #define LOAD_FONT8  // Font 8. Large 75 pixel font needs ~3256 bytes in FLASH, only characters 1234567890:-.
// #define LOAD_FONT8N // Font 8. Alternative to Font 8 above, slightly narrower, so 3 digits fit a 160 pixel TFT
// #define LOAD_GFXFF  // FreeFonts. Include access to the 48 Adafruit_GFX free fonts FF1 to FF48 and custom fonts

#define SMOOTH_FONT // MUST REMAIN ACTIVE OTHERWISE BUTTON CONFIG IS CORRUPTED for some reason....
// #define SPI_FREQUENCY  27000000
#define SPI_FREQUENCY 40000000
/*
 * To make the Library not over-write all this:
 */
#define USER_SETUP_LOADED
#endif // original EleksTube IPS clock Gen2 XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

#endif /* GLOBAL_DEFINES_H_ */
