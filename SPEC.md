# SmoothSensors Robot — Specification (Draft v1)

Status: DRAFT — under active review. Items marked TBD still need a decision before the phase that depends on them.

## 1. Purpose

An autonomous, wheeled robot that responds to voice commands, including finding and following an object (e.g. a person, a car, a cat) using onboard computer vision. No manual/remote-control mode in v1 — voice is the only control input. All commands are prefixed with the wake word "robot"; any speech not starting with "robot" is ignored.

## 2. Hardware

| Component | Role | Status |
|---|---|---|
| Arduino UNO Q | Dual-brain controller: Qualcomm QRB2210 (Linux, quad-core Cortex-A53 @ 2GHz, Adreno GPU) + STM32U585 (Zephyr RTOS, Cortex-M33) | Have it |
| Modulino Distance | Front-facing time-of-flight distance sensor | Wired, code exists (`SmoothDistance.h`) |
| Modulino Movement | IMU (accelerometer + gyro) | Wired, code exists (`SmoothMovement.h`) |
| Modulino Motors | MAX22211 dual H-bridge driver, 2 DC motors, differential drive, 5–24V/3.8A per channel motor supply, I2C/Qwiic control. Has configurable current-drive regulation (ITRIP, set via REFA/REFB resistors, up to 3.8A) — used to current-limit each motor channel, see Power row below. | Have it, not yet wired |
| Motors | [TT gear motor wheel kit](https://www.amazon.com/dp/B098Q1BCX5) (generic "TT motor," 1:48 gearbox, dual shaft, 69mm wheels). Standard TT motor specs: **rated 3–6V**, ~150mA no-load current per motor, ~1.1–1.5A stall current per motor (varies with voltage), ~200 RPM / 0.8 kg·cm stall torque at 6V. | Chosen |
| Magnetometer (LIS3MDL) | `"robot turn right/left N degrees"` still uses relative gyro dead-reckoning, not the compass (see §4). But absolute heading **is** now used, for `"robot point to <bearing>"` (§4c) — a real v1 use case, not deferred as originally planned (see §7 item 2). | **Wired, calibrated, and in active use.** `sketch/SmoothCompass.h` is wired into `sketch.ino`/the RPC surface and reads real hardware (no boot-hang — the `recoverBus()` guard holds). Raw headings were hard-iron biased (~3x Earth's field, confirmed empirically) until the motor-driven, gyro-closed calibration spin (§4b) was added and run — calibrated headings are now accurate enough to drive closed-loop turns (§4c). |
| USB camera | Front-facing; dual purpose: (a) input to onboard object-detection model, (b) live view streamed to a human over the local network. No built-in mic. | Physically present, not yet wired |
| Microphone | Captures voice commands | Mini USB microphone, attaches to the USB-C hub alongside the camera. Plugs into the Linux side, no custom MCU-side driver work. Purchased. **STT engine: Vosk** (offline, on-device) — no network dependency for core voice commands, lightweight enough for the QRB2210. |
| USB-C hub | The UNO Q has exactly **one USB-C port and no built-in USB-A ports**, so the camera and mic can't both plug in directly — a hub is required. Recommendation: **Arduino's official 8-in-1 USB-C hub for UNO Q** (USB-A 2.0, USB-A 3.0, HDMI, Ethernet, and — critically — **power delivery passthrough**, so the board still receives power through the hub while the camera and mic occupy its USB-A ports). A hub without PD passthrough would leave the board without a power path once the port is used for peripherals. | Purchased |
| Onboard LED matrix | Built into the UNO Q (8×13, 104 LEDs), driven by the STM32 MCU via the `Arduino_LED_Matrix` library. Used as the robot's only feedback display — see §4. | Built-in, have it |
| Chassis | Current: prototype. Target: 3D-printed. | Tracked as a separate design track (own mini-spec: dimensions, mount points, clearances) once electronics layout is settled — not part of this software spec. |
| Power | **Two separate rails**: one for Uno Q logic (delivered via the USB-C hub's power passthrough), one dedicated battery+regulator feeding the Modulino Motors' screw terminals directly. Avoids brownouts from motor current sagging the logic supply. Mitigation: set the MAX22211's ITRIP current limit to ~1.5A per channel (matching the motors' rated stall current) to cap heat/mechanical stress during stall. No PWM duty-cycle cap needed — the UBEC (below) holds motor rail voltage at a clean, fixed 6.0V regardless of battery charge state, so the driver never sees more than the motors' rated 6V. | [NASTIMA 6V 6Ah LiFePO4](https://www.amazon.com/dp/B0FD3SZFXF/) (2000+ cycles, built-in BMS, 10A max continuous discharge — well above the ~3A worst-case combined motor stall current) feeding a [Hobbywing 5A UBEC](https://valuehobby.com/hobbywing-5a-ubec-high-voltage.html) (switching buck regulator, output fixed to 6.0V, 5A continuous/15A instantaneous, >90% efficient) wired between the battery and the Modulino Motors' screw terminals. Raw battery voltage swings ~5–7.3V across its charge cycle (LiFePO4 cells read above 6V nominal near full charge); the UBEC clamps that to a steady 6.0V so the motors (rated 3–6V) never see an overshoot, eliminating the need for a software PWM cap. Battery-to-UBEC wiring: solder a cable from the NASTIMA's output connector to the UBEC input directly — no off-the-shelf connector match needed. Rejected earlier: [WAVYPO 9V 1300mAh Li-ion](https://www.amazon.com/dp/B0BC8P2TS6) — reviews report its internal boost converter caps steady-state output around ~100mA, far below driving current needs. | Purchased |
| Motor voltage regulator | [Hobbywing 5A UBEC](https://valuehobby.com/hobbywing-5a-ubec-high-voltage.html) — steps the LiFePO4 battery's raw ~5–7.3V down to a fixed, regulated 6.0V for the motor rail. See Power row. | Purchased |

## 3. System architecture

Two-brain split, mirroring the UNO Q hardware:

- **MCU side** (`sketch/`, Zephyr, real-time) — owns the Modulino I2C bus: Distance, Movement, Motors (Magnetometer deferred to phase 2, see §2). Responsible for low-level motion primitives (drive, turn-by-degrees, stop) and **hard safety interlocks** (tilt-stop, proximity-stop), enforced locally so they still fire even if the Linux side is slow or busy. Exposes primitives to the Linux side over RPC (extending the existing but currently-empty `get_sensor_values()` bridge).
- **Linux side** (`python/`, Debian, "brain") — owns the camera, microphone, speech-to-text (Vosk, offline), object-detection model, the local-network video stream server, and the high-level behavior loop (filter for the "robot" wake word → interpret voice command → decide behavior → issue motion requests to the MCU side). Never drives motors directly.

**Why split safety this way**: if proximity-stop and tilt-stop lived only in Python, a slow CV inference frame could delay a stop. Putting them on the MCU side is defense-in-depth — the robot stops even if the "brain" is momentarily busy.

## 4. Core behaviors

- **Voice-driven, fully autonomous.** No joystick/remote mode in v1.
- **Wake word:** every command must start with the word "robot" (e.g. "robot stop", "robot find and follow cars"). Any recognized speech that doesn't start with "robot" is discarded — no action taken, no side effects.
- **v1 command vocabulary:**
  - `"robot find and follow <object>"` → enter autonomous vision-following mode targeting the named object class (e.g. "cats", "people", "cars" — not limited to a fixed list, see below)
  - `"robot stop"` → halt all motion, exit any active mode
  - `"robot go forward"` → drive straight forward in a line
  - `"robot turn right <N> degrees"` / `"robot turn left <N> degrees"` → closed-loop turn using gyroscope feedback: integrate the Modulino Movement IMU's Z-axis rotation rate (`rz`, already exposed by `SmoothMovement::get()`) over time to track cumulative rotation since the turn started, stop once it reaches `N` degrees. No magnetometer needed — v1 turns are relative (dead-reckoned), not absolute-heading; drift over the few seconds a turn takes is negligible. See §2, Magnetometer row.
  - `"robot calibrate"` → triggers motor calibration followed by the compass calibration spin, in that sequence (MCU-side `move("calibrate")` command) → see §4b.
  - `"robot point to <bearing>"`, where `<bearing>` is one of North/Northeast/East/Southeast/South/Southwest/West/Northwest → closed-loop turn using the live, calibrated compass heading (not gyro dead-reckoning) to rotate until the robot faces the requested absolute bearing → see §4c.
  - This is the full v1 vocabulary planned so far; more can be added in a later phase once the core loop is proven.
  - No backward-motion command in v1 — the only distance sensor is front-facing, so there's no obstacle protection for reversing. Revisit once a rear-facing sensor is added.
- **Go forward:** drives continuously until `"robot stop"` is heard or a safety interlock fires — same rule as following mode. No fixed-distance mode in v1.
- **Object-following mode:**
  - The target object name is parsed out of the command (the words after "find and follow") and mapped to a class the onboard object-detection model recognizes. A single general-purpose pretrained multi-class model (e.g. one trained on COCO, which already includes person, car, cat, dog, and more) covers this without per-object custom training — good fit given the requirement isn't cat-specific.
  - Name normalization: lowercase the parsed noun, strip a trailing "s" (handles "cats"→`cat`, "cars"→`car`, "dogs"→`dog`, etc.), with a small hand-written exception table for irregulars (starting with "people"→`person`), extended as new irregular cases come up. No NLP library dependency.
  - If the requested object isn't a class the detector supports (e.g. "robot find and follow squirrels"), display error `e01` on the LED matrix (see §4a) instead of attempting the behavior.
  - Onboard object detection locates the target class in the camera frame; robot steers to keep it centered and drives toward it.
  - Stop distance from the target: reuses the proximity-stop threshold (§5) — no special-cased "following distance," just don't get closer than the general obstacle-stop distance.
  - Multiple matching objects in frame: follow whichever detected instance has the largest bounding box (a simple proxy for "closest/most prominent," needs no extra hardware); ignore the rest.
  - Lost target: rotate in place toward the side the target was last seen, sweeping up to a full 360°, to search and reacquire. If the target isn't reacquired by the time the sweep completes, stop and await a new voice command — the full rotation itself bounds the search, no separate timeout needed.
- **Live camera view:** streamed over the local Wi-Fi network for a human to watch. Observation only — never used as a control input by the human.

## 4a. Feedback display (onboard LED matrix)

The robot's only user-facing feedback channel in v1 is the built-in LED matrix (no speaker/TTS in scope):

- **Understood a command:** display `ok`.
- **Any error:** display `eNN`, where `NN` is a two-digit error code.
- **Error code registry** (extend as new error cases are identified during implementation):
  | Code | Meaning |
  |---|---|
  | `e01` | Requested object class not recognized by the detector |
  | `e02` | Compass calibration requested but a required sensor/motor isn't initialized |
  | `e03` | Compass calibration finished but the magnetic sample span was too small to compute valid offsets (likely didn't actually rotate) |
  | `e04` | Point-to-bearing timed out (8s) before reaching the target heading within tolerance — see §4c |
  | `e05` | Point-to-bearing requested but a required sensor/motor isn't initialized (mirrors `e02` for the point-to command) |
  | `e06` | Point-to-bearing requested before any compass calibration has succeeded this boot — see §4c |
- **In-progress status codes** (extend as new commands are added): `rdy` (ready at startup), `ga`/`gb`/`tr`/`tl`/`st` (go ahead / go back / turn right / turn left / stop, echoing the recognized command), `cal` (calibration succeeded), `pN`/`pNE`/`pE`/`pSE`/`pS`/`pSW`/`pW`/`pNW` (point-to-bearing in progress, shown immediately on command receipt — see §4c).
- **Persistence:** the matrix shows the result of the last command and stays that way until the next command overwrites it — no auto-clear timer. Before the first command is ever received, the matrix is blank.

Since the LED matrix hardware is driven by the MCU (`Arduino_LED_Matrix`) but recognition/parsing happens on the Linux "brain," the Linux side will need to request a display update from the MCU side over RPC (alongside the existing motion-primitive RPC calls in §3).

## 4b. Compass calibration (motor-driven, gyro-closed)

The magnetometer readings are dominated by a hard-iron offset from onboard magnetic material (motors, battery, wiring) — raw headings only sweep a narrow ~40° arc instead of the full circle. Calibration removes this by finding the offset (and axis scale, for soft-iron distortion) from a full-circle sample of raw x/y readings, then correcting all future readings against it.

- **Trigger:** voice command `"robot calibrate"` (added to the Vosk grammar in `python/VoiceCommands.py` alongside the existing move commands), which calls the MCU-side `move("calibrate")` RPC (same RPC surface as `go_ahead`/`turn_right`/`turn_left`/`stop`, see §3) — this runs motor calibration first, then the compass calibration spin described below, as a single sequenced command.
- **Rotation:** self-driven via the existing motors, not manual. Closed-loop on the gyro, using the same `rz`-integration dead-reckoning technique described for `turn right/left N degrees` (§4) — this calibration spin is the first actual implementation of that technique in the sketch; `turn right/left N degrees` itself remains unimplemented (§4 documents the intended approach only). Accumulate rotation until it passes **380°** (a 20° overshoot margin over a full circle, to absorb gyro lag/undershoot so no heading is missed), then stop. A **hard timeout (8s)** force-stops the motors regardless of accumulated angle, guarding against the robot being stuck or lifted mid-spin, where `rz` would never accumulate.
- **Safety interlock:** the proximity-stop check (§5) is suspended for the duration of calibration — it's a pure in-place spin, not forward motion — and restored once calibration ends (success or timeout).
- **Sampling:** raw magnetometer x/y are tracked (min/max per axis) throughout the spin, alongside the existing smoothed ring buffer.
- **Result:** hard-iron offset (`(max+min)/2` per axis) and soft-iron scale (`mean_radius / ((max-min)/2)` per axis) are computed once the spin ends, applied immediately in RAM (so `SmoothCompass::getDirectionAngle()` is corrected for the rest of that boot), and printed to the Monitor for the operator to copy into `SmoothCompass.h` as permanent defaults — there's no flash/EEPROM persistence in this codebase, so an in-RAM-only calibration is lost on reboot until hardcoded.
- **Not addressed by this calibration:** the dynamic field from motor current while driving (differs from the field during the calibration spin itself), and any residual mounting-rotation/declination offset — both remain open, see §7.

## 4c. Point to bearing (closed-loop compass turn)

Turns the robot in place until it faces a requested absolute bearing, using the live, calibrated compass heading as feedback rather than gyro dead-reckoning (unlike §4b's calibration spin, which has no absolute reference to close the loop against, this command's whole purpose is to use one).

- **Trigger:** voice command `"robot point to <bearing>"` for one of the 8 primary bearings (North/Northeast/East/Southeast/South/Southwest/West/Northwest), added to the Vosk grammar in `python/VoiceCommands.py`. Each maps to a distinct RPC command string: `point_north`, `point_northeast`, `point_east`, `point_southeast`, `point_south`, `point_southwest`, `point_west`, `point_northwest`. Target degrees: N=0°, NE=45°, E=90°, SE=135°, S=180°, SW=225°, W=270°, NW=315°.
- **Precondition, runtime-enforced:** a compass calibration (§4b) must have succeeded at least once since boot. This is checked in code (not just documented) — a `point_*` command issued before any successful calibration this boot is refused with error `e06`, rather than silently running against whatever `SmoothCompass.h`'s hardcoded defaults happen to be.
- **Display:** immediately on receiving the command (before the turn starts), the LED matrix shows `p` followed by the bearing's abbreviation (`pN`, `pNE`, `pE`, `pSE`, `pS`, `pSW`, `pW`, `pNW`) — this is the Python-side `main.py` `_led_codes` entry, shown the same way `ga`/`tr`/etc. are today. On completion, the MCU overwrites it with `ok` (reached the target) or `e04` (timed out).
- **Direction decision:** compute the signed angular error (target − current heading, normalized to `(-180°, 180°]`). Turning right (`turn_right`) increases the reported heading (confirmed empirically against real hardware); a positive error turns right, a negative error turns left — the shorter way around, never more than 180° of rotation.
- **Turn/settle/re-check loop, not a single pass:** `SmoothCompass::getDirectionAngle()` is a 32-sample trim-mean, so while the robot is actively spinning the reported heading lags the true one — stopping the instant the raw error looks small would systematically overshoot. Instead: turn until the error is within tolerance (**±8°**) or its sign flips (guards against jumping past the tolerance band between loop iterations), stop the motors, wait for the ring buffer to refill with post-stop samples, then re-read the heading — if still outside tolerance, turn again (opposite direction if needed) rather than declaring success on a stale reading.
- **Safety timeout:** **8 seconds**, bounding the whole operation including every turn/settle/re-check cycle — guards against a bad calibration, an unreachable target, or oscillation at the tolerance boundary. Times out to `e04`.
- **Safety interlock:** proximity-stop (§5) is suspended for the duration, same reasoning as §4b — it's a pure in-place rotation, and the interlock's own `move("stop")`/LED-overwrite behavior would otherwise cancel the turn and stomp the `pN`-style display.
- **Cancellation:** any other `move()` command received while a point-to turn is in progress cancels it immediately (mirrors §4b's calibration-cancellation behavior). Re-issuing the same bearing while already turning toward it is a no-op (the turn continues); a different bearing restarts the turn toward the new target.
- **Not addressed:** a turn speed independent of the fixed drive speed used elsewhere (`RobotMotors`'s `DRIVE_SPEED`) — if physical testing shows the tolerance/settle values above don't converge reliably, a slower turn speed for this command specifically is the likely next step, deferred until shown necessary.

## 5. Safety (always active, MCU-enforced, overrides any in-progress command)

- **Proximity stop:** if the front distance sensor reads below a threshold, stop forward motion immediately. Threshold: **5 cm**.
- **Tilt stop:** IMU is monitored continuously (not just at startup); if tilt exceeds a threshold at any time, including mid-drive, stop all motion immediately. Threshold: **30°, provisional** — ships as-is in phase 1 (the chassis is built during phase 1, but calibrating this value is deliberately sequenced as a phase 2 activity, not blocked on anything from phase 1). **Phase 2 calibration:** physical test, tilting the assembled robot degree by degree until it tumbles, to find the real threshold — reusing the 30° provisional value until then.

## 6. Explicitly out of scope for v1

- Manual/remote teleoperation
- Camera used for anything beyond object-detection + human viewing
- Following objects outside the detector's supported classes
- Voice commands not prefixed with "robot"
- Chassis CAD (separate track)

## 7. Open items still needing a decision

1. Chassis dimensions/CAD (separate track, own spec)
2. ~~Whether v1 ever needs absolute heading (magnetometer) beyond relative turn-by-degrees~~ — resolved: yes, `"robot point to <bearing>"` (§4c) is a real v1 use case, built on the calibrated compass from §4b. Still open: a mounting-rotation/declination trim constant, and whether motor-current drift while actively driving needs separate handling (§4b's "not addressed" list) — neither has caused an observed problem yet since point-to only turns in place with motors idle otherwise
3. Final tilt-stop threshold — deferred to phase 2 by choice (empirical tumble test on the assembled robot), not blocked on chassis geometry since phase 1 builds the chassis
4. `"robot find and follow <object>"` (camera + onboard object detection) — deferred out of phase 1; camera isn't wired and no detection model is chosen yet. See `PHASE1.md` for phase 1's actual scope.
5. ~~Whether the calibration spin (§4b) should be exposed as a voice command~~ — resolved: yes, `"robot calibrate"` (§4/§4b)
6. Whether calibration constants need persistent (flash/EEPROM) storage, or hardcoding into `SmoothCompass.h` after each physical remounting remains acceptable
