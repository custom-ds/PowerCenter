# Power Center

A WiFi-enabled power control center for amateur radio stations. It allows up to five power relays
(one of which is a dedicated high-power main system relay) to be switched on or off individually or
as a sequenced whole, from a built-in web UI or REST API served by the device itself.

## Overview

This project is designed to let a station operator:

- turn the whole station on/off as a single sequenced operation (avoiding inrush by staggering relay
  closures) using a front-panel button or the web UI
- turn individual relay channels on, off, or toggle them independently
- mark specific channels to stay powered through a system-wide shutdown (e.g. equipment that needs to
  stay available even when the rest of the station is off)
- label each relay channel with a custom name
- control the system from a browser or automation client over WiFi

The repository is divided into two main parts:

- [Firmware/](Firmware/) — ESP32 firmware for control logic, WiFi, API routes, and UI generation
- [PowerCenter/](PowerCenter/) — placeholder for the hardware design files (currently empty)

## Features

- 5-channel relay control (4 Qwiic relay channels + 1 high-power main relay)
- Sequenced system on/off to stagger relay closures and reduce inrush
- Per-relay "leave powered on" flag so specific channels can be exempted from a system shutdown
- Persistent relay names and configuration stored in ESP32 flash using Preferences
- Front-panel on/off button with a fading power LED to indicate system state
- Built-in web interface served by the ESP32 itself
- REST-style API for external control and automation
- WiFi configuration with optional static IP support

## Hardware

The firmware targets the SparkFun ESP32 Thing Plus C board. The controller also uses:

- SparkFun Qwiic Quad Relay board (I2C, address `0x6D`) for relays 1-4
- a directly-driven GPIO relay for relay 5, the high-power main system relay
- a front-panel momentary button (short press = system on, long press = system off) and a power LED
- external 12V supply for the relay bank

## Repository Layout

```text
.
├── Firmware/
│   ├── Firmware.ino
│   ├── config.h.template
│   ├── config.h
│   ├── README.md
│   └── LICENSE
├── PowerCenter/
├── README.md
├── LICENSE
└── CLAUDE.md
```

## Firmware Build and Upload

The firmware is developed primarily in the Arduino IDE, although it can be edited in VS Code and
uploaded from the Arduino IDE while both are pointed at the same .ino file.

### Required software

Install the following in the Arduino IDE:

1. ESP32 board package by Espressif Systems — **must be downgraded to v2.0.17** for this firmware to
   compile. The 3.x line changed the hardware timer API (`timerBegin()`) used here.
2. Required libraries:
   - AsyncTCP
   - ESPAsyncTCP
   - ESPAsyncWebSrv
   - SparkFun Qwiic Relay Arduino Library

### Important build flag for regex routes

The ESPAsyncWebSrv routes use regex matching for the API handlers. Without this flag, the web routes
will silently fail to match even though the firmware compiles cleanly.

For Arduino IDE, create or update the file:

- Windows: `%LOCALAPPDATA%\Arduino15\packages\espxxxx\hardware\espxxxx\{version}\platform.local.txt`
- Linux: `~/.arduino15/packages/espxxxx/hardware/espxxxx/{version}/platform.local.txt`

Add this line:

```text
compiler.cpp.extra_flags=-DASYNCWEBSERVER_REGEX=1
```

Then restart the Arduino IDE.

### Board selection and upload

1. Connect the ESP32 board over USB-C.
2. In Arduino IDE, choose the correct COM port.
3. Select the board: `Sparkfun ESP32 Thing Plus C`.
4. Compile and upload the firmware.

> If the relays are already installed, the board may also require external 12V input for the relay
> supply.

## Configuration

The firmware expects a local config file named `config.h` in [Firmware/](Firmware/). This file is
intentionally gitignored and must be created from the template at
[Firmware/config.h.template](Firmware/config.h.template).

The config file contains values for:

- WiFi SSID and password
- optional static IP configuration
- gateway, subnet, and DNS settings

Never commit the generated `config.h` file to version control.

## Web UI and API

The ESP32 serves a built-in web interface and REST-like endpoints for control and configuration. The
firmware generates the HTML/CSS/JS dynamically rather than using a separate frontend project.

- `GET /` — control page (toggle the system and individual relays)
- `GET /config` — configuration page (relay labels and "leave powered on" flags)
- `POST /api/{key}/set/relay/state/{relay}/{state}` — set a relay (`0` = whole system, keyed off the
  main relay) to `0` (off), `1` (on), or `2` (toggle)
- `POST /api/{key}/set/relay/name/{relay}/{name}` — rename a relay
- `POST /api/{key}/set/relay/nooff/{relay}/{0|1}` — set whether a relay stays powered on through a
  system shutdown
- `GET /api/{key}/get/relay/state/{relay}` — relay `9` returns the state of all 5 relays as
  `ON:OFF:ON:...`
- `GET /api/{key}/get/relay/name/{relay}`

Every route requires a valid API key in the path, checked against either the short-lived
per-boot session key or the long-lived key persisted in flash (both are logged to Serial at boot).

## State Persistence

The controller stores persistent settings in the ESP32 `Preferences` namespace, including:

- relay display names
- the "leave powered on during shutdown" flag per relay
- a long-lived API key

This ensures the device remembers its configuration between power cycles.

## Project Notes

- There is no package manager or automated CI/test suite in this repo.
- Firmware development is hardware-focused and is flashed via the Arduino IDE.
- If the hardware design files under [PowerCenter/](PowerCenter/) are added, keep them and this README
  in sync.

## More Information

For the most detailed firmware guidance, including required libraries, upload procedure, and
environment setup, see [Firmware/README.md](Firmware/README.md).

## License

This project is distributed under the terms of the license included in [LICENSE](LICENSE).
