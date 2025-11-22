# MarvelTubes Clock

## Identified components

- MCU **ESP32** as ESP32-S2-WROVER module, see [datasheet](https://documentation.espressif.com/esp32-s2-wroom_esp32-s2-wroom-i_datasheet_en.pdf)
- Flash 16MB in the module
- PSRAM 2MB in the module
- RTC **ECS-RTC-3225-5609 Clone** in SMD3225 package, see [datasheet](https://ecsxtal.com/store/pdf/ECS-RTC-3225-5609-Full-Spec.pdf) -> I2C ID 0x32 -> Seems to be an RX8025T clone
- UART Chip **CH340** as CH340C, see [datasheet](https://web.archive.org/web/20230328023924/http://wch-ic.com/downloads/file/79.html?time=2023-01-31%2005:37:01&code=byrlQteadwoMguMjmbWlPKyCiEACwGGapSxnN8I1)
- Audio **NAU88C22** 24-bit Stereo Audio Codec with Speaker Driver [datasheet](https://www.nuvoton.com/resource-files/NAU88C22DataSheet0.6.pdf)
- Generic active buzzer
- **LCD panel**
	- a generic FPC ST7789 panel (135x240), see [datasheet](https://www.buydisplay.com/download/manual/ER-TFT1.14-2_Datasheet.pdf)
	- WS2812B, see [datasheet](https://cdn-shop.adafruit.com/datasheets/WS2812B.pdf)
- Transistor for LCD control (on/off and dimming) - Marked A79T - should be an AO3407 - see https://alltransistors.com/mosfet/transistor.php?transistor=16072
- Default "auto download mode" circuit for ESP32
- AMS1085 - voltage regulator, see [datasheet](http://www.advanced-monolithic.com/pdf/ds1085.pdf)
