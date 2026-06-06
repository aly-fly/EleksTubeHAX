# MarvelTubes Gen2 Clock

## Identified components

- MCU **ESP32** as ESP32-WROOM-32E module
  - Standard ESP32 (Xtensa LX6 dual-core, ROM: "ets Jul 29 2019 12:21:46")
  - Flash **16MB** built into the module (no external flash chip)
  - No PSRAM
- UART Chip **CH340** as CH340C, see [datasheet](https://web.archive.org/web/20230328023924/http://wch-ic.com/downloads/file/79.html?time=2023-01-31%2005:37:01&code=byrlQteadwoMguMjmbWlPKyCiEACwGGapSxnN8I1)
- Audio **NAU88C22** 24-bit Stereo Audio Codec with Speaker Driver [datasheet](https://www.nuvoton.com/resource-files/NAU88C22DataSheet0.6.pdf)
  - Control interface: **I2C **
    - **Confirmed: CSB HIGH → I2C mode, address 0x1A** (verified via WorkingTest2.cpp)
  - I2C control GPIO assignments: **SDA=GPIO33, SCL=GPIO25**
  - Audio output: I2S — **BCLK=GPIO0, LRCK/WS=GPIO13, SDIN(data to codec)=GPIO32**
  - PLL source: BCLK (3.072 MHz @ 16-bit stereo 48 kHz), VCO/2=12.288 MHz SYSCLK
- **No RTC** — no real-time clock chip, no backup battery on Gen2
- Generic active buzzer (GPIO unknown, need discovery)
- **LCD panels** × 6
  - Generic FPC ST7789 panel (135×240), see [datasheet](https://www.buydisplay.com/download/manual/ER-TFT1.14-2_Datasheet.pdf)
  - Backlight LEDs: **WS2812B** × 6, see [datasheet](https://cdn-shop.adafruit.com/datasheets/WS2812B.pdf)
- Transistor for LCD backlight control (on/off and PWM dimming) — marked **A79T** (AO3407 P-channel MOSFET), see [datasheet](https://alltransistors.com/mosfet/transistor.php?transistor=16072)
  - Active LOW (GPIO26 drives gate; LOW = backlights ON)
- Default "auto download mode" circuit for ESP32 (EN + GPIO0 buttons)
- **AMS1085** voltage regulator, see [datasheet](http://www.advanced-monolithic.com/pdf/ds1085.pdf)

## GPIO Pin Assignment (confirmed)

| GPIO | Function | Direction | Notes |
|------|----------|-----------|-------|
| 4    | WS2812B data (backlights) | out | |
| 5    | LCD CS — Minutes Tens | out | CS_DIRECT_GPIO |
| 14   | LCD CS — Seconds Ones | out | CS_DIRECT_GPIO |
| 16   | TFT SPI SCLK | out | VSPI CLK |
| 17   | LCD CS — Minutes Ones | out | CS_DIRECT_GPIO |
| 18   | TFT SPI MOSI | out | VSPI MOSI |
| 19   | LCD CS — Hours Ones | out | CS_DIRECT_GPIO; was hijacked as VSPI MISO by TFT_eSPI — fixed by reclaimPins() |
| 21   | TFT DC (Data/Command) | out | |
| 22   | LCD CS — Hours Tens | out | CS_DIRECT_GPIO |
| 23   | TFT RST (Reset) | out | |
| 26   | TFT backlight enable (PWM dim) | out | Active LOW via AO3407; PWM channel 0 |
| 0    | I2S BCLK (NAU88C22) | out | Strapping pin — boot mode; shared with auto-download circuit |
| 13   | I2S LRCK/WS (NAU88C22) | out | |
| 25   | NAU88C22 I2C SCL | out | |
| 27   | LCD CS — Seconds Tens | out | CS_DIRECT_GPIO |
| 32   | I2S SDIN — data to NAU88C22 | out | |
| 33   | NAU88C22 I2C SDA | in/out | |
| 34   | Button LEFT (Style) | in-only | active LOW, external pull-up |
| 35   | Button MODE (Menu) | in-only | active LOW, external pull-up |
| 36   | Button POWER (Alarm) | in-only | active LOW, external pull-up |
| 39   | Button RIGHT (Time) | in-only | active LOW, external pull-up |

## Free output-capable GPIOs (unassigned)

`2`, `12`, `15`

These are candidates for: buzzer, or other future use.

Note: `0` (I2S BCLK) is also the ESP32 boot strapping / auto-download button pin — use with care.

## Firmware notes

- Flash partition: `partition_16MB.csv` — nvs@0x9000, app@0x10000 (2MB), littlefs@0x210000 (~13.3MB)
- Custom board definition required: `boards/esp32devmarveltubesgen2.json` (flash_size=16MB, flash_mode=dio)
- No `Wire.begin()` should be called at startup (no RTC) — otherwise I2C bus contention on TFT_DC/CS pins
- CS method: `CS_DIRECT_GPIO` — each display has its own GPIO, `reclaimPins()` must be called after `TFT_eSPI::init()`
