# AGENTS.md — EleksTubeHAX

## Agent instructions

- Chat responses: use the language of the IDE/system locale (Windows: `Get-Culture` / `$PSUICulture`,
  Linux/macOS: `LANG`). Never hardcode a language; if no locale is available, mirror the user's message.
- Code comments, identifiers, `Serial.print` / debug output, commit messages, documentation: **English**
- **Never commit or push on your own!** Only after an explicit instruction from the user.
- **Never switch branches** unless explicitly asked to.
- **Never push to a `main` branch unless the user explicitly asks for it.** Confirm first and run
  `git branch --show-current`. For changes to this project, open a pull request against the
  upstream repository instead of committing to a main branch.
- **Never open a pull request into a fork's own `main`.** PRs always target the upstream
  repository. Example:
  `gh pr create --repo <upstream-owner>/EleksTubeHAX --base main --head <github-user>:<branch>`
- **Do not sync or update a fork's `main`** (`gh repo sync`, `git push origin main`) unless the
  user asks for it - that is the fork owner's decision.
- **Never stash, pop, reset, or run any other git operation** without explicit permission.
- **Any action that changes the branch or working tree (edit, commit, push, stash, pop, reset, checkout) requires the user's approval first.**
- `include/_USER_DEFINES.h` contains WiFi/MQTT credentials. It may be read for build context, but **never print, quote, copy or commit its secret values**. Use `include/_USER_DEFINES - empty.h` when showing configuration examples.
- **No machine-local or private values in tracked files.** COM ports, credentials, host names and
  machine-specific paths never go into `platformio.ini`, `AGENTS.md`, `_USER_DEFINES - empty.h`,
  docs, code or commit messages. Never add `upload_port` / `monitor_port` to the tracked
  `platformio.ini`; pass the port on the command line instead
  (`pio run -e <env> -t upload --upload-port <PORT>`).
- **Local-only control files** are `.local/**` (machine-specific values such as the COM port map,
  plus local procedures) and `include/_USER_DEFINES.h`. They may be read when a task needs their
  content, but their values must never be printed, quoted, copied into tracked files or committed.
  Editor- and agent-specific scratch files (rule folders, prompt/instruction files of any AI
  coding tool) are git-ignored too and are not part of the project - see `.git/info/exclude`.
- **Building is allowed, flashing is not.** `pio run -e <env>` may be run freely. `-t upload`,
  `-t uploadfs`, `pio device monitor` and every `esptool` call happen only after an explicit user
  request and only with the port the user names. The local procedure is
  `.local/flash-and-monitor.md` (local only; if it is absent, ask the user for the port and the
  download-mode procedure).
- **Never use `--erase-all` or `write_flash` from offset `0x0`** without explicit confirmation; it
  wipes the LittleFS clock faces and any backup stored on the clock.

## Guardrails

- Do not modify unless explicitly requested: `lib/modified_*/**`, `include/_USER_DEFINES.h`,
  `platformio.ini` (`build_flags`, partitions, `BUILDVER`), `.github/workflows/**`, `boards/*.json`.
- After code changes, verify with `pio run -e <affected env>`. Do not build all environments
  locally - CI does that. The first build per environment downloads the toolchain (several minutes).
- Preserve line endings: LF for all text files, CRLF only for `*.cmd` / `*.bat` (see `.gitattributes`).
- No wholesale reformatting, no unrelated refactors, no renames inside a change request.
- Before any commit or push: run `git branch --show-current` and `git status`. If the current
  branch is `main` (or `master`), confirm with the user before committing there.
- Pull request flow: branch off the fork's `main`, push the branch to `origin` (the contributor's
  fork), open the PR against the upstream repository
  (`gh pr create --repo <upstream-owner>/EleksTubeHAX --base main --head <github-user>:<branch>`)
  and report the PR URL. Do not merge PRs, do not push to `upstream`, do not force-push, do not
  delete branches.
- No test framework exists; verification = successful build (flashing is done by the user).

## Repository hygiene

- `firmware/` holds the binaries of the current release only. When `BUILDVER` is bumped, the
  previous `FW_*_v<old>.bin` files are removed with `git rm` in the same commit. GitHub Releases
  keep the history of all versions; the repository keeps only the latest set.
  Helper: `python scripts/clean_old_firmware.py` (dry run) and `--apply --git-rm` (execute).
- `firmware/*.bin`, `firmware/esptool.exe`, `data/*.bmp` and `assets/**` are **intentionally**
  committed. Never delete them, never "clean up" binaries. Git LFS is deliberately not used -
  do not propose it.
- These files are binary and are not meant to be read or searched: `firmware/*.bin`, `*.bmp`,
  `*.clk`, `*.jpg`, `*.pdf`, `*.exe`. Do not open them; ask if the content is actually required.
- `lib/modified_TFT_eSPI/examples/` (313 sample sketches) is only relevant for example-related
  tasks - do not search it for project code.
- `.ignore` keeps large/binary content out of the agent context. It does not affect Git tracking.

## Remotes

- `origin` = the contributor's own fork of this repository - push feature branches here.
- `upstream` = the original repository - the PR target; never push there.
- Additional remotes may exist for fetching other contributors' work; treat them as fetch-only.
- Whether and when a fork's `main` is synced with upstream is the fork owner's decision.

Check the actual setup with `git remote -v`.

## Quick start

```bash
# Build for a specific clock model
pio run -e EleksTube          # Original EleksTube (4MB flash)
pio run -e IPSTube            # IPSTube H401/H402 (8MB flash, custom board)
pio run -e MarvelTubes        # ESP32-S2, 16MB flash, custom board
pio run -e MarvelTubesMini    # ESP32-C3, 80x160 displays, I2C IO expander
pio run -e DEsign             # Touch rings, 80x160 displays

# Upload firmware + filesystem
pio run -e EleksTube -t upload       # app partition
pio run -e EleksTube -t buildfs      # build LittleFS image
pio run -e EleksTube -t uploadfs     # upload LittleFS (clock faces)

# Combined firmware binary (enable -D CREATE_FIRMWAREFILE first)
pio run -e EleksTube

# Serial monitor (MarvelTubesMini: requires monitor_dtr=0, monitor_rts=0)
pio device monitor
```

## Project structure

- `src/main.cpp` — Arduino `setup()` / `loop()`, the single entry point
- `include/GLOBAL_DEFINES.h` — per-hardware pin mappings, TFT config, TFT_eSPI `User_Setup.h` replacement
- `include/_USER_DEFINES.h` — user-copied from `include/_USER_DEFINES - empty.h`; **gitignored** (contains WiFi/MQTT credentials)
- `include/*.h` / `src/*.cpp` — one module per feature (Backlights, Buttons, Clock, Menu, TFTs, MQTT, WiFi, IPGeolocation)
- `boards/*.json` — custom board definitions (IPSTube, MarvelTubes variants, Xunfeng S2, etc.)
- `lib/modified_*` — locally patched library copies that shadow PIO registry versions
- `data/` — LittleFS filesystem root (clock face BMP/CLK images + `clockfaces.txt`)
- `scripts/` — pre/post build scripts (TFT config, firmware rename, mini clockface rescaling, unified binary builder)
- `firmware/` — pre-built binaries + esptool.exe + CMD flash/backup scripts
- `tools/` — BMP→CLK converter, image prep tools

## Build system quirks

**TFT_eSPI is locally modified, not from registry.** The `bodmer/TFT_eSPI` line in `lib_deps` is commented out. The local copy at `lib/modified_TFT_eSPI/` is compiled in automatically from `lib/`. Same for NTPClient, RTC_RX8025T, and SparkFun APDS9960.

**Pre-build scripts run in order defined per environment.** MarvelTubesMini and DEsign run `script_prepare_mini_clockfaces.py` *first* (resizes 135×240 → 80×160), then `script_configure_tft_lib.py`, then `script_rename_firmware.py`. Omitting the first script for those envs will build with wrong-sized images.

**Unified firmware binary:** uncomment `-D CREATE_FIRMWAREFILE` in `platformio.ini` `[env]`, then `pio run`. Post-build `script_build_unified_binary.py` runs `buildfs` + `esptool.py merge-bin`. Output: `.pio/build/<env>/FW_<CLOCK>_<VERSION>.bin`.

## Architecture

- **6 TFT digits**, indexed as `HOURS_TENS(5)`..`SECONDS_ONES(0)` (reversed for PunkCyber/Xunfeng)
- **Chip select**: shift register (74HC595) for most clocks; direct GPIO for IPSTube, MarvelTubes, DEsign; I2C IO expander for MarvelTubes Mini
- **Display size**: 135×240 (ST7789) for full-size clocks; 80×160 (ST7735) for MarvelTubesMini and DEsign
- **Digit order** is defined in `GLOBAL_DEFINES.h` — PunkCyber/Xunfeng have reversed order (`SECONDS_ONES`=5)
- **RTC**: DS3231 (I2C), DS1302 (3-wire), RX8025T (I2C), or NTP-only (MarvelTubes Gen2/Mini)
- **MQTT topic root**: `elekstubehax/<device_id>/...` where `<device_id>` = `lower(DEVICE_NAME)-<last3MACbytes>`
- **Image format**: BMP (palettized recommended) or CLK (proprietary RGB565). Set via `USE_CLK_FILES` in `_USER_DEFINES.h`

## Key `_USER_DEFINES.h` toggles

| Define | Effect |
|--------|--------|
| `WIFI_USE_WPS` | WPS push-button (else hardcoded SSID/pass) |
| `DIMMING` | Night-time dimming (defines `NIGHT_TIME`/`DAY_TIME`) |
| `MQTT_PLAIN_ENABLED` | MQTT without Home Assistant |
| `MQTT_HOME_ASSISTANT` | HA auto-discovery via MQTT |
| `GEOLOCATION_ENABLED` | IP-based timezone auto-detection |
| `USE_CLK_FILES` | Use .CLK instead of .BMP images |

Only one MQTT mode at a time. Only one geolocation provider at a time.

## Hardware-specific notes

- **IPSTube**: slow Winbond flash → SPI forced to DIO 40MHz. QIO/80MHz causes instability. One button only (`ONE_BUTTON_ONLY_MENU`). Optional LED stripe (uncomment `HARDWAREMOD_IPSTUBE_CLOCK_WITH_LED_STRIPE`). Missing Q1 transistor disables hw dimming (`HARDWAREMOD_IPSTUBE_CLOCK_WITHOUT_DIMMING_TRANSISTOR`).
- **MarvelTubes**: ESP32-S2, needs fake MISO(45)/CS(46) pins for SPI. SPI at 20MHz only.
- **MarvelTubes Gen2**: ESP32, no RTC (NTP-only). SPI at 20MHz.
- **MarvelTubes Mini**: ESP32-C3, USB-CDC (DTR/RTS must be disabled). I2C IO expander for CS + dimming. No direct PWM dimming. ST7735 80×160.
- **DEsign**: Capacitive touch rings (no physical buttons). `touchRead()` threshold 25. ST7735S 80×160, BGR color order. SPI at 27MHz.
- **NovelLife SE**: Gesture sensor (APDS9960) instead of buttons. Fake APDS chip → gain forced to 1×.
- **PunkCyber**: CH340 chip often missing — use external USB-UART bridge. Identical to EleksTube at firmware level (`#define HARDWARE_PUNKCYBER_CLOCK` maps to `HARDWARE_ELEKSTUBE_CLOCK`).

## Testing

No test framework is present. Verification is done by building with `pio run` and flashing to hardware.

## CI (GitHub Actions)

Three workflows exist in `.github/workflows/`:
- `pio-build-multi-platform.yml` — build all environments
- `pio-build-and-publish-all-firmware-files.yml` — build + publish artifacts
- `pio-build-and-create-github-release.yml` — build + create release

All triggered on push/pull_request (check exact triggers in the workflow files).
