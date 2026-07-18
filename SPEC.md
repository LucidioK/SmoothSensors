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
| Modulino Motors | MAX22211 dual H-bridge driver, 2 DC motors, differential drive, 5–24V/3.8A per channel motor supply, I2C/Qwiic control | Have it, not yet wired |
| Magnetometer (LIS3MDL) | Compass heading, for absolute-heading driving and precise turns | Library already pinned in `sketch/sketch.yaml`, not yet wired |
| USB camera | Front-facing; dual purpose: (a) input to onboard object-detection model, (b) live view streamed to a human over the local network. No built-in mic. | Physically present, not yet wired |
| Microphone | Captures voice commands | Mini USB microphone, attaches to the USB-C hub alongside the camera. Plugs into the Linux side, works with standard ALSA/PyAudio/STT tooling, no custom MCU-side driver work. Purchasing. |
| USB-C hub | The UNO Q has exactly **one USB-C port and no built-in USB-A ports**, so the camera and mic can't both plug in directly — a hub is required. Recommendation: **Arduino's official 8-in-1 USB-C hub for UNO Q** (USB-A 2.0, USB-A 3.0, HDMI, Ethernet, and — critically — **power delivery passthrough**, so the board still receives power through the hub while the camera and mic occupy its USB-A ports). A hub without PD passthrough would leave the board without a power path once the port is used for peripherals. | Needed, not yet purchased |
| Onboard LED matrix | Built into the UNO Q (8×13, 104 LEDs), driven by the STM32 MCU via the `Arduino_LED_Matrix` library. Used as the robot's only feedback display — see §4. | Built-in, have it |
| Chassis | Current: prototype. Target: 3D-printed. | Tracked as a separate design track (own mini-spec: dimensions, mount points, clearances) once electronics layout is settled — not part of this software spec. |
| Power | **Two separate rails**: one for Uno Q logic (delivered via the USB-C hub's power passthrough), one dedicated battery pack feeding the Modulino Motors' screw terminals directly (5–24V). Avoids brownouts from motor current sagging the logic supply. | Chemistry/capacity TBD — depends on the actual motors' stall current, decide in the hardware phase. |

## 3. System architecture

Two-brain split, mirroring the UNO Q hardware:

- **MCU side** (`sketch/`, Zephyr, real-time) — owns the Modulino I2C bus: Distance, Movement, Motors, Magnetometer. Responsible for low-level motion primitives (drive, turn-to-heading, stop) and **hard safety interlocks** (tilt-stop, proximity-stop), enforced locally so they still fire even if the Linux side is slow or busy. Exposes primitives to the Linux side over RPC (extending the existing but currently-empty `get_sensor_values()` bridge).
- **Linux side** (`python/`, Debian, "brain") — owns the camera, microphone, speech-to-text, object-detection model, the local-network video stream server, and the high-level behavior loop (filter for the "robot" wake word → interpret voice command → decide behavior → issue motion requests to the MCU side). Never drives motors directly.

**Why split safety this way**: if proximity-stop and tilt-stop lived only in Python, a slow CV inference frame could delay a stop. Putting them on the MCU side is defense-in-depth — the robot stops even if the "brain" is momentarily busy.

## 4. Core behaviors

- **Voice-driven, fully autonomous.** No joystick/remote mode in v1.
- **Wake word:** every command must start with the word "robot" (e.g. "robot stop", "robot find and follow cars"). Any recognized speech that doesn't start with "robot" is discarded — no action taken, no side effects.
- **v1 command vocabulary:**
  - `"robot find and follow <object>"` → enter autonomous vision-following mode targeting the named object class (e.g. "cats", "people", "cars" — not limited to a fixed list, see below)
  - `"robot stop"` → halt all motion, exit any active mode
  - `"robot go forward"` → drive straight forward in a line
  - `"robot turn right <N> degrees"` / `"robot turn left <N> degrees"` → closed-loop turn using magnetometer feedback
  - This is the full v1 vocabulary — no additional commands planned; more can be added in a later phase once the core loop is proven.
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
- **Persistence:** the matrix shows the result of the last command and stays that way until the next command overwrites it — no auto-clear timer. Before the first command is ever received, the matrix is blank.

Since the LED matrix hardware is driven by the MCU (`Arduino_LED_Matrix`) but recognition/parsing happens on the Linux "brain," the Linux side will need to request a display update from the MCU side over RPC (alongside the existing motion-primitive RPC calls in §3).

## 5. Safety (always active, MCU-enforced, overrides any in-progress command)

- **Proximity stop:** if the front distance sensor reads below a threshold, stop forward motion immediately. Threshold: **5 cm**.
- **Tilt stop:** IMU is monitored continuously (not just at startup); if tilt exceeds a threshold at any time, including mid-drive, stop all motion immediately. Threshold: **30°, provisional** — reasonable starting point, but the right value depends on the final chassis geometry (height/width), which isn't designed yet; revisit once the chassis is built.

## 6. Explicitly out of scope for v1

- Manual/remote teleoperation
- Camera used for anything beyond object-detection + human viewing
- Following objects outside the detector's supported classes
- Voice commands not prefixed with "robot"
- Chassis CAD (separate track)

## 7. Open items still needing a decision

1. Motor battery chemistry/capacity (blocked on motor selection)
2. Chassis dimensions/CAD (separate track, own spec)
