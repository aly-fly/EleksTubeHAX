# EleksTubeHAX — IPSTube H401 custom firmware

A fork of [EleksTubeHAX](https://github.com/aly-fly/EleksTubeHAX) (PlatformIO, ESP32,
Arduino framework), customised for an **IPSTube H401** clock (six IPS displays). The
clock is a **pure MQTT subscriber**: it shows what it's told to show. Everything else
(querying data, formatting it, deciding what to show) lives in scripts on a separate
Linux server, never on the ESP32.

Upstream's own documentation is in `README.md` and `docs/`.

## Hard rules

1. **Claude never flashes the clock.** The user uploads firmware by hand, from the
   machine the clock is plugged into. When giving upload commands, put a check for
   the original-firmware backup first: about 8 MB, read from *this* unit, since the
   stock firmware is locked to the device's MAC address. If there's no backup, say so
   before anything else.
2. **Firmware and filesystem are two uploads.** Give both, in order:
   `pio run -e IPSTube_S3 -t upload`, then `pio run -e IPSTube_S3 -t uploadfs`.
   Uploading the firmware alone leaves the displays blank. Over the S3's native USB,
   esptool normally enters download mode by itself; if it can't connect, or the board
   is boot-looping (the USB port keeps vanishing), hold the button while plugging in
   USB-C.
3. **WiFi credentials are hardcoded in `include/_USER_DEFINES.h`**, which is
   gitignored. Never print, log or echo its contents in chat, commit messages or any
   file outside the working tree. The build copies it to
   `lib/modified_TFT_eSPI/_USER_DEFINES.h` (also gitignored), so the password lives
   there too. The template (`_USER_DEFINES - empty.h`) ships with `WIFI_USE_WPS`
   enabled; it has to be commented out.
4. **Built binaries are confidential.** This fork is public, but any `.bin` built
   locally contains the WiFi password in plaintext. Never attach, commit or link one
   in a Release, Issue, PR or anywhere public. (The CI release workflows build from
   the empty template, so their output carries no credentials.) Don't enable
   `-D CREATE_FIRMWAREFILE` for local builds: it copies the merged image into the
   tracked `firmware/` directory, where it would be committed with everything else.
5. **No data fetching on the ESP32.** No QuestDB queries, SQL, or HTTP calls to fetch
   sensor data. If a feature seems to need that, the logic belongs in a server-side
   script: flag it rather than implementing it on the device. The one exception is
   fetching image files (below).
6. **A clean build proves nothing visual.** After a change, ask the user to flash it
   and look at the clock. Work one display mode at a time, and don't stack up
   unverified changes.

## Architecture

```
QuestDB ──SQL──▶ publisher script (server) ──MQTT──▶ broker ──MQTT──▶ ESP32 clock
```

- MQTT via `MQTT_PLAIN_ENABLED`, with a self-defined topic schema. Not
  `MQTT_HOME_ASSISTANT`, and not the SmartNest-shaped protocol the plain mode was
  written for. Its template defaults the broker to `smartnest.cz`, so change it.
- MQTT code: `include/MQTT_client_ips.h`, `src/MQTT_client_ips.cpp`. The existing
  `subscribe()` calls are the pattern for new topics. Their handlers call into
  `Backlights` (colour) and `TFTs` (images); feature flags go in `GLOBAL_DEFINES.h`.
- A trigger can start local behaviour that then runs without further MQTT traffic
  (e.g. a countdown).

### Images: trigger, then fetch

MQTT carries only a small trigger (an ID, filename or URL), never image bytes,
because PubSubClient's buffer and the ESP32's RAM can't handle them well. The firmware
does an HTTP GET for the image, writes it to LittleFS, and displays it like any
clock-face image.

Animation (multi-frame sequences) is deferred, but this mechanism has to extend to it
without a rewrite:

- The trigger payload can express "frame N of M", e.g.
  `{"id": "...", "frame": 0, "total_frames": 1}`, even though only single images are
  sent for now.
- The fetch routine is a generic "fetch one named file", callable repeatedly.
- LittleFS names are structured (ID plus frame number), not one file overwritten in
  place.
- Fetching and storing is a separate step from deciding what to display.
- Don't assume that one fetch means one display update.

## Hardware

- This unit is the **ESP32-S3 revision** of the H401, not the classic-ESP32 board
  upstream's `IPSTube` env targets. Use PlatformIO env `IPSTube_S3` (board
  `esp32s3_ipstube_8MB`, `-DIPSTUBE_S3`), cherry-picked from upstream PR #175; the S3
  pin overrides are in `GLOBAL_DEFINES.h` and `ChipSelect.cpp`. Never build or flash
  the plain `IPSTube` env for it.
- 8 MB flash. `partition_8MB.csv` gives app `0x130000` (~1.2 MB) and LittleFS
  `0x6B0000` (~6.7 MB).
- USB is the S3's native USB-Serial/JTAG (no CH340); the serial port is usually
  `/dev/ttyACM0`.
- Keep `-DUSE_HSPI_PORT` in `[env:IPSTube_S3]`: without it TFT_eSPI crashes on the
  first display init and the board boot-loops.
- No RTC has been found on the S3 board; time comes from NTP.
- Upstream's Q1 / `HARDWAREMOD_IPSTUBE_CLOCK_WITHOUT_DIMMING_TRANSISTOR` setting is for
  the classic board and doesn't apply. On the S3 the displays are dimmed by PWM on
  GPIO9, which works on this unit; leave that flag off.
- Upstream says the IPSTube's SPI flash is slower than on other clocks this firmware
  supports. That isn't checked for the S3 revision, so still don't design for
  frequent flash writes.
- Images are at most 135 × 240 px per display, **24-bit RGB BMP** (not palettised).
- One button only. No gesture sensor, no LED strip, no front LEDs.
- Upstream detects at most about 8 clock-face sets.

## Display modes (initial set)

| Mode | Display 1–6 | Notes |
|---|---|---|
| Clock | `H H M M S S` | Native behaviour |
| Household power | `d . d d k W` | e.g. `3.42 kW`; clamp above 9.99 rather than erroring |
| Temperature & weather | `± d d ° C icon` | `°` and `C` glyphs not made yet |
| Pictures | 6 tiles | One image pre-sliced into 6 on the server; a trigger picks the set |
| Countdown | digits | Triggered by MQTT, counted locally, at most one update per second; HH:MM:SS vs MM:SS undecided |

Digit images (0–9) are shared by every numeric mode. Images are stored by display
position, not by what a numeral means, so don't make a separate digit set per mode.

## Rejected approaches (don't reintroduce without discussion)

- Image bytes sent directly over MQTT.
- Home Assistant / SmartNest MQTT conventions.
- WPS WiFi setup, and the pre-built release binaries (they only support WPS).

## Repo

- Default branch `main`. Work on feature branches. `main` tracks upstream.
- Branch `ipstube-s3` carries the S3 support (upstream PR #175, not merged upstream
  yet). Base feature work on it; if #175 is merged, rebase onto `main`.
- Build check: `pio run -e IPSTube_S3`. There's no test suite, and anything visual
  needs the device. The build needs `include/_USER_DEFINES.h`; for a check without
  credentials, copy the empty template to it and delete both copies afterwards (rule 3).
- Runs via claude-code-docker: only the Squid allowlist is reachable, and there's no
  USB device access in the container.
- Machine-specific details are in the untracked `CLAUDE.local.md`.
