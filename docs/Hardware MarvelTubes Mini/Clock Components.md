# MarvelTubes Mini Clock

## Identified components

- **MCU** ESP32-C3 as **ESP32-C3-Mini-1** module, 160 MHz, see [datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-c3-mini-1_datasheet_en.pdf)
  - Flash: 4 MB embedded in module
  - RAM: 400 KB SRAM (320 KB usable for application)
  - USB: Built-in USB Serial/JTAG controller (no separate USB-UART chip)
  - WiFi 802.11 b/g/n + Bluetooth 5 (LE)

- **USB Interface** via built-in ESP32-C3 USB Serial/JTAG — no external CH340 or CP210x chip

- **RTC** — **not populated** in the standard kit. Soldering pads for an RTC (I2C) are present on the PCB. Could be added manually and connected via the I2C bus (not tested).

- **IO Expander** — unknown 14-pin custom chip at address I2C `0x19` (non-standard, not PCA9554-compatible)
  - Controls chip select (CS) lines for all 6 displays (active low, one per pin)
  - Controls display backlight power and dimming via a dedicated register
  - Controls display Reset line (shared, via transistor Q5 / Q3)
  - Requires a specific init replay sequence (register `0x01`: `0xFE → 0xFC → 0xFE`)
  - **WARNING**: Register `0x03` must NOT be written — corrupts chip state and hangs SPI bus

- **TFT Display breakout boards** — 6× identical 10-pin breakout boards, each containing:
  - **ST7735** based 80×160 pixel IPS TFT display (RGB 16-bit color)
  - 1× **WS2812B** RGB LED (addressable, for backlight effect)
  - Pin layout: VCC, LEDA/VCC, GND, Reset, RS/DC, SDA, SCL, CS, RGB-LED data

- **WS2812B** RGB LEDs — one per display breakout board, 6 total, chained on GPIO 6

- **Buttons** — 4 buttons, active low, externally pulled up:
  - GPIO 9 — Style / Left
  - GPIO 0 — Menu / Mode
  - GPIO 1 — Time / Right
  - GPIO 2 — Alarm / Power

- **SPI Bus** (to all TFTs via IO expander CS):
  - MOSI: GPIO 8
  - SCLK: GPIO 7
  - DC/RS: GPIO 10
  - MISO: GPIO 5 (dummy, required for ESP32-C3 SPI to work — not connected to displays)
  - CS: via IO expander (I2C), no direct GPIO CS
  - Reset: via IO expander, no direct GPIO Reset

- **I2C Bus** (to IO expander):
  - SDA: GPIO 3
  - SCL: GPIO 4

## Notes

- No shift register (74HC595) — chip selects are handled entirely via I2C IO expander
- No hardware PWM dimming pin — brightness is controlled via IO expander register `0x02` (value 0–255)
- No RTC battery holder in the kit — time is kept via NTP only
- `monitor_dtr = 0` / `monitor_rts = 0` required in PlatformIO to prevent resets when opening/closing the serial monitor (ESP32-C3 USB-CDC reacts to DTR/RTS)
