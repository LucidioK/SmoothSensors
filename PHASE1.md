# SmoothSensors — Phase 1

Status: DRAFT. Scope agreed 2026-08-14: non-vision commands only (`stop`, `go forward`, `turn <left|right> <N> degrees`), audio input, and LED matrix feedback, all backed by the MCU-side safety interlocks. No magnetometer, no camera/object-detection (`"find and follow"`) — those are deferred to a later phase (see `SPEC.md` §7). This file assumes `SPEC.md` as the source of truth for behavior/thresholds and only breaks it into buildable, testable steps.

Scenarios are ordered by dependency — each one after #2 can be built and manually tested standalone before the next.

## 1. Robot has an assembled physical chassis

- Gather real-world dimensions for every component that needs mounting, per `SPEC.md` §2: UNO Q board footprint, Modulino Motors module, TT motor + 69mm wheel kit, NASTIMA battery pack, Hobbywing UBEC, USB-C hub, USB camera, microphone, Modulino Distance sensor (needs a clear forward-facing view)
- Design the chassis as parametric FreeCAD Python scripts (`cad/`), driven by those dimensions — 4 bolted parts: top plate (sensors + UNO Q), battery box, and two motor mounts (left/right, below the battery)
- Render/export the design to STL via `cad/build_all.py` (`freecadcmd cad/build_all.py`)
- Obtain the 3D-printed parts (local printer or a print service)
- Assemble the chassis: mount the UNO Q board, Modulino Motors + motors/wheels, battery + UBEC, USB-C hub, camera, microphone, Distance sensor
- Basic tests: confirm physical fit (no interference between mounted parts, wheels turn freely by hand), confirm the chassis sits flat and stable at rest, confirm the board still boots and Distance/Movement sensors still read correctly once everything is mounted (checks for interference introduced by assembly, e.g. metal/wiring near the sensors)

## 2. Robot's motors are wired and powered

- Wire the Modulino Motors' screw terminals to the NASTIMA LiFePO4 battery via the Hobbywing UBEC, per `SPEC.md` §2 (Power row)
- Set the MAX22211's ITRIP to ~1.5A/channel via the REFA/REFB resistors
- Power on and confirm the board still boots normally and the Distance/Movement sensors still read correctly with the motor rail connected (check for brownout or I2C interference from motor switching noise)
- Bench-test: each wheel spins in both directions at a low duty cycle, driven by a throwaway test sketch (not the real primitives yet) — confirms wiring polarity and the driver chip before building on top of it

## 3. Robot's brain and controller can talk to each other

- Define the RPC message contract between `python/` and `sketch/` for: motion commands (`stop`, `driveForward`, `turn(direction, degrees)`) and LED display requests (`ok`, `eNN`)
- Implement the MCU-side RPC handlers in `sketch.ino`, extending the currently-empty `get_sensor_values()` bridge pattern
- Implement the Linux-side RPC client calls in `python/`
- Round-trip test: call a trivial RPC method from Python, confirm the MCU receives it and responds (e.g. echo or LED blink)

## 4. Robot stops on command

- Implement the `stop()` motion primitive on the MCU side (zero both motor channels)
- Wire it to the RPC handler from scenario 3
- Manual test: trigger `stop` via a hardcoded Python script call, confirm motors halt

## 5. Robot drives forward on command

- Implement the `driveForward()` motion primitive (both motors, same direction and duty cycle)
- Wire it to its RPC handler
- Manual test: trigger via script, confirm straight-line forward motion, confirm a follow-up `stop` call halts it

## 6. Robot turns left/right by a given number of degrees on command

- Confirm on real hardware whether `SmoothMovement`'s `rz` (from `_movement.getYaw()`) is an angular *rate* (°/s, as the code comment in `SmoothMovement.h` claims) or an already-integrated absolute angle — this determines whether the primitive needs to do its own time integration or can just read a running value. (No Modulino library source was available locally to confirm ahead of time.)
- Implement a dead-reckoned `turn(direction, degrees)` primitive: track cumulative rotation since the turn started (integrating `rz` over elapsed time if it's a rate) until it reaches the requested degrees, then stop
- Wire it to its RPC handler (direction + degrees as parameters)
- Manual test: trigger `turn right 90`, verify actual rotation against a physical reference mark (e.g. tape line on the floor), tune for overshoot/settling error

## 7. Robot stops automatically when something gets too close

- Implement a continuous MCU-side proximity check (5cm threshold, `SPEC.md` §5) that overrides any in-progress motion primitive, independent of RPC command cadence
- Manual test: drive forward via scenario 5, place an obstacle within 5cm, confirm the robot stops on its own with no Python-side involvement

## 8. Robot stops automatically when it tips too far

- Implement a continuous MCU-side tilt check using the existing `SmoothMovement` accelerometer data, provisional 30° threshold (`SPEC.md` §5) — this value ships as-is in phase 1; the degree-by-degree tumble test to find the real threshold is a phase 2 activity, not phase 1
- Same override behavior as scenario 7
- Manual test: tilt the assembled chassis past the (provisional) threshold mid-drive, confirm autonomous stop

## 9. Robot shows simple status messages on its LED matrix

- Add an `Arduino_LED_Matrix`-based display helper on the MCU side implementing `SPEC.md` §4a (`ok`, `eNN`, persists until overwritten, blank before the first command)
- Add the MCU-side RPC handler for display requests (from scenario 3's contract)
- Wire the Linux-side RPC call
- Manual test: trigger `ok` and a sample `eNN` from Python, confirm the matrix updates and persists

## 10. Robot receives orders by audio

- Integrate Vosk (offline STT) into `python/`: model selection/download, ALSA/PyAudio capture pipeline
- Continuous transcript loop; discard any utterance not starting with the wake word "robot" (`SPEC.md` §4)
- Command parser mapping recognized text to the phase 1 command set (`stop`, `go forward`, `turn <left|right> <N> degrees`) — reuse the object-name-normalization approach from `SPEC.md` §4 as a pattern, but only wire the non-vision commands now
- Any command outside the phase 1 set (including `"find and follow"`, deferred per scenario scope) or anything unparseable → trigger `eNN` on the LED matrix (scenario 9) instead of attempting the behavior
- Wire parsed commands to the RPC calls from scenarios 4–6
- Manual test: speak each phase 1 command aloud, confirm correct motion + LED feedback end-to-end
