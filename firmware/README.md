# EleksTubeHAX - An aftermarket custom firmware for the desk clock

## 1. Pre-built firmware files

In this folder you can find pre-built firmware image files that you can directly upload/flash to your clock.

These binaries may have limited functionality compared to a self-built firmware, because not all available features are enabled or can be configured.

The image files (clock faces) are fixed and can't be changed without building your own firmware file with the development tools (see README.md)!

### 1.1 Pre-configured functionality

The pre-built images are built with the **web setup** (`WEB_SETUP_PORTAL`). No compiling is needed for your own settings.

Enabled:

- Web setup: WiFi and the MQTT broker are entered on a web page served by the clock (see section 5 "First start").
- Home Assistant integration via MQTT auto discovery (an MQTT broker, e.g. the Mosquitto add-on of Home Assistant, is required).
- Night time dimming - by default from 22:00h (10 pm) to 07:00h (7 am). Can be switched on/off and changed from Home Assistant.
- IP-based geolocation - time zone & DST come from IP-API.com. Can be switched off (manual offset) from Home Assistant.

Not available in the pre-built images (needs a self-built firmware without `WEB_SETUP_PORTAL`, see README.md in the root):

- WiFi setup via WPS or hard coded WiFi credentials.
- Plain MQTT without Home Assistant (e.g. SmartNest.cz) and MQTT over TLS.

Disabled:

- Debug outputs via serial interface.
- For IPSTube clocks:
  - LED strip: off by default, switch it on with the option *LED strip fitted* on the setup page.
  - Hardware dimming enabled.

General:

- 7 different sets of clock faces present. See [data](https://github.com/aly-fly/EleksTubeHAX/tree/main/data).
- BMP mode active for the given clock faces
- CORE_DEBUG_LEVEL=0 - No ESP32 core messages via serial interface.

## 2. Backup your original firmware

**Always backup YOUR clock firmware version as first step!**

Save your original firmware using the script that matches your device (`_ESP32 save flash 4MB.cmd`, `_ESP32 save flash 8MB.cmd` for the IPSTube, or `_ESP32 save flash 16MB.cmd` for the MarvelTubes) by changing the COM port to the number your clock uses.

Rename and store the `backup1.bin` on a save location.

See also the section "Backup first" and following in the `README.md` file in the root.

## 3. Available image files

In this folder you will find the flashable files.
They are updated from time to time in the repository, so version number may vary.

The newest pre-built firmware files are published as **GitHub Releases** and can be downloaded directly from the [Releases page](https://github.com/aly-fly/EleksTubeHAX/releases).

Last Update: 2026-11-25 to Version 1.3.16

| clock model | firmware image file |
| -- | -- |
| EleksTube IPS - Orginal Version | `FW_EleksTube_v1.3.16.bin` |
| EleksTube IPS - Gen2 models | `FW_EleksTube_Gen2_v1.3.16.bin` |
| SI HAI IPS | `FW_SI_HAI_v1.3.16.bin` |
| Xunfeng IPS | `FW_Xunfeng_v1.3.16.bin` |
| NovelLife SE version | `FW_NovelLife_v1.3.16.bin` |
| PunkCyber/RGB Glow Tube DIY | `FW_PunkCyber_v1.3.16.bin` |
| IPSTUBE - Model H401 and H402 with ESP32 | `FW_IPSTube_v1.3.16.bin` |
| IPSTUBE - Model H401 and H402 with ESP32-S3 | `FW_IPSTube_Gen3_v1.3.16.bin` |
| MarvelTubes | `FW_MarvelTubes_v1.3.16.bin` |
| MarvelTubes Gen2 | `FW_MarvelTubes_Gen2_v1.3.16.bin` |
| MarvelTubes Mini | `FW_MarvelTubesMini_v1.3.16.bin` |
| D-Esign Clock | `FW_DEsign_v1.3.16.bin` |

Note: All "Original" EleksTube clocks, sold after July 2022 are "Gen2" versions. See [Note on EleksTube website](https://elekstube.com/blogs/news/instructions-on-elekstube-clock-for-gen2-systems). But always check the PCB version of your clock!

Note for IPSTube clocks: The backup script (section 2) shows the chip of your clock: `Chip is ESP32-D0WD...` -> `FW_IPSTube_*.bin`, `Chip is ESP32-S3...` -> `FW_IPSTube_Gen3_*.bin`.

## 4. Write the EleksTubeHAX firmware file

- Choose the right pre-built firmware file for your clock.
- Edit the file `_ESP32 write flash.cmd` with an editor.
- Write the correct COM port of your clock.
- Write the correct firmware file name for your clock.
- Run the CMD file.

The CMD file should look like this:

`esptool --port COM5 --baud 921600 write_flash --erase-all 0x0000 FW_EleksTube_v1.3.16.bin`

Note: Most clocks will go into to the "download mode" automatically when esptool is trying to write to it.
Some clocks needs a button pressed while the powering phase (plugging the USB cable) to enter this mode, like the IPSTUBE ones.

**Note for Xunfeng and MarvelTubes**: These use an ESP32-S2 chip. If auto-detection fails, add `--chip esp32s2` to the esptool command.

**Note for MarvelTubes Mini**: Uses an ESP32-C3 with built-in USB Serial/JTAG — no CH340 driver needed. If auto-detection fails, add `--chip esp32c3`.

**Note for IPSTube with ESP32-S3** (`FW_IPSTube_Gen3_*.bin`): If auto-detection fails, add `--chip esp32s3`.

## 5. First start (web setup)

After flashing, the clock has no WiFi settings (`--erase-all` clears them, also on every later flash with the CMD file). It opens the web setup:

1. The displays show "WEB SETUP" and the name of an open WiFi network, named after the clock (e.g. `ipstube-a1b2c3`).
2. Join this network with a phone or PC and open `http://192.168.4.1` (most phones open the page automatically).
3. Select your WiFi network and enter its password, the MQTT broker of your Home Assistant (host, port, user, password) and - on IPSTube clocks only - whether the LED strip is fitted.
4. Press *Save and restart*. The clock joins your WiFi and shows up in Home Assistant (MQTT integration).

If nothing is entered within 15 minutes, the clock continues without WiFi. To change the settings later, open the clock menu and go to the last item *Web setup*. More details: section "Web setup" in the README.md in the root.

## 6. Download the newest firmware files

The newest pre-built firmware files are published as **GitHub Releases** and can be downloaded directly from the [Releases page](https://github.com/aly-fly/EleksTubeHAX/releases).

Each release contains all firmware binaries for all supported hardware variants. Download the `FW_*.bin` file that matches your clock model.

The downloaded firmware file can be flashed to the clock by modifying the existing CMD file or using esptool directly (see section 4).

## 7. There is no warranty of any type

Use at your own risk!

If you mess-up your clock, it's only your fault!
