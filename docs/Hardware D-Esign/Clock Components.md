# D-Esign IPS Tube Clock

## Identified Components

- **MCU** ESP32 as **ESP32-WROOM-32** module, 240 MHz, see [datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32_datasheet_en.pdf)
  - Flash: 4 MB embedded in module
  - RAM: 520 KB SRAM
  - WiFi 802.11 b/g/n + Bluetooth 4.2
  - USB: via external USB-UART bridge (CH340 or CP210x)

- **RTC** — **DS3231** real-time clock, connected via I2C:
  - SCL: GPIO 22
  - SDA: GPIO 23

- **TFT Displays** — 6× **ST7735S** based 80×160 pixel IPS TFT displays (panel: NF P096H-09B)
  - RGB color, 16-bit color depth
  - BGR color order (confirmed: 0xF800 RGB red appears blue without correction)
  - SPI frequency: 27 MHz (stable, exceeds ST7735S spec of ~15 MHz)
  - Chip Select lines directly connected to individual GPIOs (no shift register, no I2C expander)

- **WS2812B** RGB LEDs — 6 total, one per display, chained:
  - Data line: GPIO 19 → R5 → Gate of Q5, Source=GND, Drain → R25 → DIN LED1 → … → LED6

- **Touch Buttons** — 2× capacitive metal rings, connected to ESP32 capacitive touch inputs:
  - Left ring: GPIO 32 (TOUCH9)
  - Right ring: GPIO 33 (TOUCH8)
  - `touchRead()` used instead of `digitalRead()` — lower value = touched (idle ≈ 38–51, touched ≈ 4–8)
  - Touch threshold: 25

- **Buzzer** — connected to GPIO 21 (confirmed by GPIO scan)

- **Microphone** — onboard **LM386MX-1** microphone amplifier; ADC output on GPIO 36 (VP, input-only ADC pin)

- **Display Backlight Enable** — GPIO 15 controls LEDA via transistor Q4 (LOW = ON)

## GPIO Assignment

| GPIO | Function |
|------|----------|
| 2    | SPI SCLK (TFT clock) |
| 4    | TFT RST (Reset, shared) |
| 12   | CS — Minutes Ones (3rd from right) |
| 13   | SPI MOSI / SDA |
| 14   | CS — Minutes Tens (3rd from left) |
| 15   | TFT Backlight LEDA enable (via Q4, LOW = ON) |
| 16   | SPI DC / RS (Data/Command) |
| 17   | CS — Hours Tens (leftmost) |
| 18   | CS — Hours Ones (2nd from left) |
| 19   | WS2812B LED data |
| 21   | Buzzer |
| 22   | I2C SCL (RTC) |
| 23   | I2C SDA (RTC) |
| 25   | CS — Seconds Ones (rightmost) |
| 26   | CS — Seconds Tens (2nd from right) |
| 32   | Touch button LEFT (TOUCH9) |
| 33   | Touch button RIGHT (TOUCH8) |
| 36   | Microphone (ADC, input-only) |

## SPI Bus (to TFT displays)

- MOSI: GPIO 13
- SCLK: GPIO 2
- DC/RS: GPIO 16
- RST: GPIO 4 (shared for all displays)
- CS: per-display GPIO (see table above) — no library-managed CS, manually controlled
- MISO: not connected (write-only, `TFT_SDA_READ` mode)

## Notes

- No shift register (74HC595) — chip selects handled by direct GPIO writes
- No I2C IO expander — direct GPIO CS control per display
- No Mode/Power buttons — only Left and Right capacitive touch buttons
- Capacitive touch: metal rings on the clock body, not mechanical buttons
- Display color order requires BGR correction (`TFT_RGB_ORDER TFT_BGR`)
- Unused / unknown GPIOs: 0, 5, 27
- Input-only GPIOs (ADC): 34, 35, 39 (unused/unidentified)
