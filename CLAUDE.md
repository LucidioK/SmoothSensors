# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

An Arduino App Lab project for the Arduino UNO Q (a dual-processor board: a Linux-side MCP and an MCU/microcontroller side). An App Lab project pairs two halves that run simultaneously and talk to each other over RPC:

- `sketch/` — the MCU-side C++ program (Arduino sketch), reads the physical sensors.
- `python/` — the Linux-side Python program (`arduino.app_utils` App framework), runs the higher-level app loop.
- `app.yaml` — the App Lab manifest tying the two halves together (name, ports, bricks).

There is no shared build tool across both halves — the sketch is compiled/uploaded via `arduino-cli`/App Lab tooling, and the Python side runs directly via the App framework runtime on the board's Linux side.

## Commands

Compiling the sketch (uses the build profile defined in `sketch/sketch.yaml`):
```
arduino-cli compile --profile default sketch
```

Running/managing the app as a whole via the App CLI (from the project root, i.e. the directory containing `app.yaml`):
```
arduino-app-cli app start .
arduino-app-cli app logs .
arduino-app-cli app stop .
```

There are no linters, formatters, or automated tests configured in this repo.

## Architecture

**Sensor smoothing pattern**: Both `SmoothDistance` (`sketch/SmoothDistance.h`) and `SmoothMovement` (`sketch/SmoothMovement.h`) use the same fixed-size circular buffer technique: a `REGISTER_COUNT`-sized ring buffer (`_position` wraps via `%= REGISTER_COUNT`) that `record()` fills from the raw Modulino sensor, and a getter that averages the buffer *after dropping the min and max sample* (a simple outlier-rejecting mean). When editing one of these classes, check whether the equivalent change belongs in the other for consistency.

- `SmoothDistance` wraps a `ModulinoDistance` (VL53L4 time-of-flight sensor) and exposes `getDistanceCm()`.
- `SmoothMovement` wraps a `ModulinoMovement` (IMU) and exposes `get(&ax, &ay, &az, &rx, &ry, &rz)` — accelerometer + roll/pitch/yaw, each independently smoothed.

**Sketch main loop** (`sketch/sketch.ino`): `record()` is called on every `loop()` iteration for both sensors (keeps the ring buffers fresh), but Monitor output (`showDistance()`, `showMovement()`) and the LED heartbeat only happen once per second, gated by a `millis()`-based `timespan` check.

**Linux/MCU RPC bridge**: `sketch.ino` includes `Arduino_RouterBridge.h` and declares `RpcCall get_sensor_values()` — this is the entry point meant to be called from the Python side over the App Lab bridge, but it is currently unimplemented (empty body). `python/main.py` does not yet call it; wiring this up is the natural next step when connecting the two halves.

**Board/library constraints**: `sketch/sketch.yaml` pins the platform to `arduino:zephyr` and lists exact library versions (Modulino, RouterBridge, LSM6DSOX, etc.). When adding sensor code, add the corresponding library dependency here with a pinned version rather than assuming it's globally installed — profile-based builds are isolated from system-wide library installs.
