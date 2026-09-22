# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

PowerCenter (W0ZC Power Center) is an ESP32-based power control center for amateur radio stations. It switches 5
relay channels (4 Qwiic relays + 1 high-power digital relay used as the main system power) and is controlled over
WiFi via a small built-in web UI and a REST-like API. There is no PC/mobile app — the ESP32 itself serves the HTML,
JS, and CSS.

The repository currently contains only the `Firmware/` sketch. The top-level `PowerCenter/` directory is a
placeholder (presumably intended for KiCad hardware design files) and is currently empty.

## Hardware

- MCU: SparkFun ESP32 Thing Plus C
- Relays: SparkFun Qwiic Quad Relay (I2C address `0x6D`) driving relays 1-4; relay 5 is a direct GPIO (`relay5` pin
  12) used as the main power relay for the system.
- Front panel: an on/off button (pin 27, active-low with pull-up) and a power LED (pin 33, PWM-faded via a hardware
  timer interrupt to indicate power state).

## Firmware — Build & Programming

This is an Arduino sketch (`Firmware/Firmware.ino`), not a standard CMake/Make project — there is no CLI build/test
command. To build/flash, use the Arduino IDE:

1. Install ESP32 board support via Boards Manager (search "esp32" by Espressif Systems). **Must be downgraded to
   v2.0.17** — the 3.x line changed the `timerBegin()` API and will not compile against this sketch.
2. Install libraries via Library Manager: `AsyncTCP` (dvarrel), `ESPAsyncTCP` (dvarrel), `ESPAsyncWebSrv` (dvarrel),
   `SparkFun Qwiic Relay Arduino Library`.
3. Enable regex routing support required by `ESPAsyncWebSrv` (the sketch uses regex route patterns like
   `^\/api\/([A-Z0-9]+)\/set\/relay\/state\/...`). Without this, routes silently 404 while the firmware still
   compiles cleanly:
   - Create/edit `platform.local.txt` (Windows:
     `C:\Users\<user>\AppData\Local\Arduino15\packages\espxxxx\hardware\espxxxx\<version>\platform.local.txt`) with:
     `compiler.cpp.extra_flags=-DASYNCWEBSERVER_REGEX=1`
   - Restart the Arduino IDE after editing.
   - (For PlatformIO instead, the equivalent is `build_flags = -DASYNCWEBSERVER_REGEX` in `platformio.ini`, though
     this project is currently set up for the Arduino IDE.)
4. Copy `Firmware/config.h.template` to `Firmware/config.h` and fill in WiFi SSID/password and static IP settings
   (`config.h` is gitignored since it holds credentials).
5. Select board "SparkFun ESP32 Thing Plus C" and the correct COM port, then Upload.
6. Editing is commonly done in VS Code with the Arduino IDE left open in the background to actually trigger
   compile/upload (edits in VS Code are picked up automatically; the IDE window is only used to click Upload).

There are no automated tests in this repository; verification is done on real hardware via the Serial monitor
(115200 baud) and the web UI.

## Firmware Architecture

Everything lives in the single file `Firmware/Firmware.ino`. Key pieces:

- **Config**: `config.h` (gitignored, real credentials) vs. `config.h.template` (checked-in example) — defines
  `ssid`, `password`, and static IP settings (`useStaticIP`, `local_IP`, `gateway`, `subnet`, DNS).
- **Relay abstraction**: relays are numbered 1-5 everywhere in the API and internal state (`relayStatus[5]`).
  `setRelay`/`getRelay` treat relay `0` as an alias for the whole system, which is keyed off relay 5 (the main power
  relay). Relays 1-4 go through the Qwiic quad relay (I2C); relay 5 is direct `digitalWrite`.
  `setSystem(state)` sequences power-up (relays 1→4, then 5, with delays between each) and power-down (reverse
  order), and respects a per-relay "leave powered on" flag (`getRelayNoOff`/`setRelayNoOff`, persisted in
  `Preferences`/NVS) so specific channels can stay energized across a system shutdown.
- **Persistence**: relay display names and "no off" flags are stored in `Preferences` (ESP32 NVS) under the
  `"power"` namespace, with keys like `rly1`, `rly1nooff`. `loadRelays()` reads names at boot; `factoryReset()`
  (currently only invocable by uncommenting a call in `setup()`) restores defaults and regenerates the API key.
- **API key / auth**: a long-term API key is generated on first boot and persisted in `Preferences`
  (`getAPIKey`/`generateKey`, 14 chars from a look-alike-free character set). A separate `sessionAPIKey` is
  regenerated every boot and embedded directly into the served JavaScript (`getJavascript()`); `checkAPIKey()`
  accepts either key. This is the only auth mechanism — the API key is treated as a bearer token in the URL path,
  not a secret verified via headers/TLS.
- **HTTP API** (regex routes registered in `setup()`, key embedded in the path):
  - `POST /api/{key}/set/relay/state/{relay 0-5}/{0|1|2}` — set/toggle a relay; relay `0` = whole system.
  - `POST /api/{key}/set/relay/name/{relay}/{name}` — rename a relay (truncated to 20 chars).
  - `POST /api/{key}/set/relay/nooff/{relay}/{0|1}` — set the "leave powered on during shutdown" flag.
  - `GET /api/{key}/get/relay/state/{relay}` — relay `9` is special-cased to return all 5 states as
    `ON:OFF:ON:...`.
  - `GET /api/{key}/get/relay/name/{relay}`.
  - `GET /`, `GET /config` — serve the control and configuration HTML pages.
  - `GET /main.js`, `GET /main.css` — served from Arduino string literals (`getJavascript()`/`getCSS()`), not
    separate files.
- **Web UI**: server-rendered HTML built from raw string literals with `%PLACEHOLDER%` token substitution (see
  `getHeader`/`getFooter`/`getPageInterface`/`getPageConfiguration`), using Bootstrap 5 and jQuery from CDN plus a
  small amount of vanilla-JS `fetch()` calls to hit the API above. The control page polls
  `GET .../get/relay/state/9` every 500ms (`tmrRefresh`) to keep button states in sync with actual relay state.
- **Power LED behavior**: a hardware timer ISR (`onTimer0`, ~10Hz) fades the power LED in/out when the system is off
  and holds it solid when on, entirely independent of the main `loop()`.
- `loop()` only handles the physical on/off button (short press = system on, long press ≥ ~500ms = system off); all
  networking is handled asynchronously by `ESPAsyncWebSrv`.
- **I2C concurrency**: `loop()` (button handling) and `ESPAsyncWebSrv` route handlers run in different tasks, and
  both can call `setRelay()`/`setSystem()`, which talk to the Qwiic quad relay over I2C. A global `i2cMutex`
  (FreeRTOS semaphore, created in `setup()`) is held around every such call to prevent two tasks from interleaving
  I2C transactions, which can wedge the bus and trigger a watchdog reset under network load. Any new code path that
  touches `quadRelay`/`Wire` from either `loop()` or a route handler must take this mutex the same way.
- **Diagnostics**: `printResetReason()` logs why the ESP32 last rebooted (power-on, watchdog, brownout, panic, etc.)
  to Serial at the very start of `setup()`, to help diagnose unexpected field reboots without guessing.
- `WiFi.setSleep(false)` is set once connected — WiFi modem power-save timing has been observed to interact badly
  with `AsyncTCP` under network load and contributes to hangs/resets.

## Conventions to Preserve When Editing

- Relay numbering is 1-based externally (API, `relayDescriptions`) but arrays are 0-based internally — off-by-one
  conversions (`relay - 1`) are used throughout; keep this consistent when adding relay-related code.
- New API routes should follow the existing `{key}` in the first regex capture group, validated via
  `checkAPIKey()`, with `request->send(400, "text/plain", ...)` for invalid input/auth.
- HTML/JS/CSS are generated as C++ raw string literals (`R"(...)"`) with `%TOKEN%` replacement rather than a
  templating library or separate front-end build — keep new UI additions in this style rather than introducing a
  build step.
