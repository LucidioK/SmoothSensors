# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

An Arduino App Lab project for the Arduino UNO Q (a dual-processor board: a Linux-side MCP and an MCU/microcontroller side). An App Lab project pairs two halves that run simultaneously and talk to each other over RPC:

- `sketch/` — the MCU-side C++ program (Arduino sketch), reads the physical sensors.
- `python/` — the Linux-side Python program (`arduino.app_utils` App framework), runs the higher-level app loop.
- `app.yaml` — the App Lab manifest tying the two halves together (name, ports, bricks).

There is no shared build tool across both halves — the sketch is compiled/uploaded via `arduino-cli`/App Lab tooling, and the Python side runs directly via the App framework runtime on the board's Linux side.

The board is developed against over the network, not locally — see "Deploying to the board" below.

## Commands

Local syntax check only (does not upload; requires `arduino-cli` and the `arduino:zephyr` core installed locally, which most dev machines won't have — the real build happens on the board, see below):
```
arduino-cli compile -b arduino:zephyr:unoq --profile default sketch
```

**Deploying to the board** (compiles the sketch, uploads it via the onboard SWD debug interface, and (re)starts the Python app, all in one step):
```
scripts/deploy.sh [board-user@host]
```
Defaults to `$BOARD_HOST`, or `arduino@10.0.0.245` if unset. Requires SSH key-based access to the board (`ssh-copy-id`, or manually append the pubkey to the board's `~/.ssh/authorized_keys` — password auth doesn't work non-interactively). The script pushes `sketch/`, `python/` (except `python/model/`, uploaded once and then left alone — see below), and `app.yaml` to `~/ArduinoApps/smoothsensors03/` on the board via `tar` over `ssh` (the board has no `rsync`), then runs `arduino-app-cli app restart` there.

Managing the running app directly via SSH (`app_path` is `ArduinoApps/smoothsensors03` on the board):
```
ssh <board-user@host> arduino-app-cli app logs ArduinoApps/smoothsensors03 --follow
ssh <board-user@host> arduino-app-cli app stop ArduinoApps/smoothsensors03
ssh <board-user@host> arduino-app-cli app restart ArduinoApps/smoothsensors03
```

The Vosk speech model (`python/model/`, tens of MB, gitignored) is uploaded once by `scripts/deploy.sh` the first time it's missing on the board, then left alone on later deploys — delete `~/ArduinoApps/smoothsensors03/python/model/` on the board to force a re-upload (e.g. after switching models).

There are no linters, formatters, or automated tests configured in this repo.

## Architecture

**Sensor smoothing pattern**: Both `SmoothDistance` (`sketch/SmoothDistance.h`) and `SmoothMovement` (`sketch/SmoothMovement.h`) use the same fixed-size circular buffer technique: a `REGISTER_COUNT`-sized ring buffer (`_position` wraps via `%= REGISTER_COUNT`) that `record()` fills from the raw Modulino sensor, and a getter that averages the buffer *after dropping the min and max sample* (a simple outlier-rejecting mean). When editing one of these classes, check whether the equivalent change belongs in the other for consistency.

- `SmoothDistance` wraps a `ModulinoDistance` (VL53L4 time-of-flight sensor) and exposes `getDistanceCm()`.
- `SmoothMovement` wraps a `ModulinoMovement` (IMU) and exposes `get(&ax, &ay, &az, &rx, &ry, &rz)` — accelerometer + roll/pitch/yaw, each independently smoothed.

**Sketch main loop** (`sketch/sketch.ino`): `record()` is called on every `loop()` iteration for both sensors (keeps the ring buffers fresh), but Monitor output (`showDistance()`, `showMovement()`) and the LED heartbeat only happen once per second, gated by a `millis()`-based `timespan` check.

**Linux/MCU RPC bridge**: `sketch.ino` includes `Arduino_RouterBridge.h` and uses the library's global `Bridge` singleton (bound internally to `Serial1` — do not declare a separate `BridgeClass` instance, it has no default constructor). MCU-side functions are exposed to Python with `Bridge.provide("name", fn)` in `setup()`; `python/main.py` calls them with `Bridge.call("name", ...)` (imported via `from arduino.app_utils import App, Bridge`). Currently registered: `show_text(String)` (drives `LedMatrixDisplay`) and `move(String)` (drives `ModulinoMotors`, command strings `"go_ahead"`/`"turn_right"`/`"turn_left"`/`"stop"`).

**Voice commands** (`python/VoiceCommands.py`): offline recognition via Vosk, grammar-constrained to the wake word "robot" followed by one of the four move commands above. Captures audio by spawning `arecord` as a subprocess (not PyAudio — the board's Python venv has no C compiler, so PyAudio's native extension can't build there) and feeding raw PCM to the recognizer.

**Board/library constraints**: `sketch/sketch.yaml` pins the platform to `arduino:zephyr` and lists exact library versions (Modulino, RouterBridge, LSM6DSOX, etc.). When adding sensor code, add the corresponding library dependency here with a pinned version rather than assuming it's globally installed — profile-based builds are isolated from system-wide library installs.
