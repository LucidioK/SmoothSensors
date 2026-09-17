#ifndef _ROBOT_MOTORS_
#define _ROBOT_MOTORS_ 1
#include <Arduino.h>
#include <Arduino_Modulino.h>
#include <Arduino_RouterBridge.h>
#include "IRobotMotors.h"
#include "ISmoothCompass.h"
#include "ISmoothMovement.h"
#include "ILedMatrixDisplay.h"
#include "MonitorFormat.h"

class RobotMotors : public IRobotMotors
{
private:
  static const uint8_t DRIVE_SPEED = 90;
  static const uint8_t MAX_SPEED_PERCENT   = 100;                                  // ModulinoMotors::setSpeedA/B silently reject >100
  static const uint8_t MAX_STRAIGHT_CORRECTION = MAX_SPEED_PERCENT - DRIVE_SPEED;  // clamp on the live straight-line correction -- headroom above DRIVE_SPEED, ceiling from setSpeedA/B's 100 limit (==10 today)
  static constexpr int STRAIGHT_CORRECTION_INTERVAL_MS = 200; // ~20 loop iterations at the measured ~8-11ms loop period -- longer than SmoothMovement's 16-sample ring buffer refresh time (~130-175ms), so every correction acts on a buffer that's fully refreshed since the last one. Makes the control gain "per second," not "per loop iteration."
  static constexpr float STRAIGHT_DEADBAND_DPS = 3.0f; // same value/idiom as MotorCalibration::RZ_DEADBAND_DPS ("rz is effectively at idle")
  static const int8_t STRAIGHT_CORRECTION_STEP = 1; // one bias unit is ~1.6 dps of yaw authority; deliberately smaller than the deadband so a single step can never carry rz across the deadband and cause chatter/limit-cycling
  static constexpr float TURN_CALIBRATED_MIN_DPS = 10.0f; // "has turn calibration actually run" gate
  static constexpr int MAX_TURN_DURATION_MS = 8000; // sanity ceiling on a single blocking turn pulse -- matches
                                                       // MotorCalibration's own TURN_MAX_DURATION_MS; a computed
                                                       // duration beyond this means the calibration data can't be
                                                       // trusted for this turn, so fall back to the uncalibrated path
                                                       // rather than blindly blocking for an unbounded time.
  ModulinoMotors _motors;
  bool _ok = false;
  String _status = "";
  bool  _straightActive = false;          // a go_ahead/go_back run is currently under live yaw correction
  bool  _straightReversed = false;        // true while the active run is go_back
  float _idleStateRz = 0;                 // issue #10's _idleStateRz -- a DISTINCT member from _idleRz1 (turn calibration, issue #8). Do not merge or reuse either one.
  bool  _idleStateRzCaptured = false;     // false until the first real capture -- guards against trusting the 0.0f default (real gyro zero-rate bias is rarely exactly 0) if go_ahead/go_back is ever the very first move() this boot
  int8_t _straightCorrection = 0;         // ephemeral per-run correction; +ve => wheel A (left) faster. Reset to 0 at the start of each straight run, never persisted across runs.
  int   _straightLastCorrectionAt = 0;
  float _idleRz1 = 0;
  float _minimumRzWhenTurningRight = 0;
  int   _timeInMillisecondsToReachMinimumRzWhenTurningRight = 0;
  int   _timeInMillisecondsToStopWhenTurningRight = 0;
  float _maximumRzWhenTurningLeft = 0;
  int   _timeInMillisecondsToReachMaximumRzWhenTurningLeft = 0;
  int   _timeInMillisecondsToStopWhenTurningLeft = 0;

  static const uint8_t BRAKE_POWER_PERCENT = 50; // % of the just-commanded speed used for the reverse-power braking pulse
  static const int BRAKE_PULSE_MS = 120;         // duration of the reverse-power pulse -- long enough to arrest momentum, short enough not to reverse travel

  bool _lastInvertA = false;
  bool _lastInvertB = false;
  uint8_t _lastSpeedA = 0;
  uint8_t _lastSpeedB = 0;

  // Heading tolerance for "close enough" to a point-to-bearing target, absorbing residual smoothing lag and post-calibration magnetic noise.
  static constexpr float POINT_TOLERANCE_DEGREES = 8.0f;
  // Hard timeout for a point-to-bearing turn, bounding every turn/settle/re-check cycle. Turns are now
  // either an exact blocking timed pulse (calibrated) or the original compass-threshold loop (uncalibrated fallback).
  static constexpr int POINT_TIMEOUT_MS = 20000;
  // Minimum dwell time after stopping before trusting a heading reading, giving the ring buffer time to refill with post-stop samples.
  static constexpr int POINT_SETTLE_MS = 500;

  /// @brief States for a point-to-bearing turn: idle, actively turning, or settling before re-checking heading.
  enum PointState { POINT_IDLE, POINT_TURNING, POINT_SETTLING };
  /// @brief Current state of any in-progress point-to-bearing turn.
  PointState _pointState = POINT_IDLE;
  /// @brief RPC command string for the current point-to-bearing turn (e.g. "point_north"), kept for Monitor output and re-trigger detection.
  String _pointCommand = "";
  /// @brief Target heading, in degrees, for the current point-to-bearing turn.
  float _pointTargetDegrees = 0;
  /// @brief Timestamp when the current point-to-bearing operation started; bounds the whole operation via POINT_TIMEOUT_MS.
  int _pointStartedAt = 0;
  /// @brief Timestamp when the current settle period began.
  int _pointSettleStartedAt = 0;
  /// @brief Loop iterations elapsed during the current settle period; waited until >= SmoothCompass::getSampleCount() before trusting the ring buffer.
  unsigned int _pointSettleSamples = 0;
  /// @brief Signed heading error (degrees) from the previous iteration, used to detect a sign flip (overshoot) between loop iterations.
  float _pointLastError = 0;
  /// @brief Timestamp when the current point-to-bearing turn (drive phase) started.
  int _pointTurnStartedAt = 0;
  /// @brief Precomputed active-drive duration (ms) for the current turn; -1 sentinel = not using a timed cutoff (RobotMotors not yet calibrated for turn rate).
  int _pointTurnDurationMs = -1;

  ISmoothCompass& _compass;
  ISmoothMovement& _movement;
  ILedMatrixDisplay& _display;

  static uint8_t _clamp(int value) { return (uint8_t)constrain(value, 0, MAX_SPEED_PERCENT); }

  void _drive(bool invertA, bool invertB, int speedA, int speedB) {
    uint8_t clampedA = _clamp(speedA);
    uint8_t clampedB = _clamp(speedB);
    _lastInvertA = invertA;
    _lastInvertB = invertB;
    _lastSpeedA = clampedA;
    _lastSpeedB = clampedB;
    _motors.setInvertA(invertA);
    _motors.setInvertB(invertB);
    _motors.setSpeedA(clampedA);
    _motors.setSpeedB(clampedB);
  }

  // Applies a brief reverse-power pulse opposing whatever the motors were just commanded to do,
  // to arrest momentum before cutting power -- coasting to a stop after a turn leaves the robot
  // spinning under momentum for several hundred ms, overshooting any heading-based stop point.
  void _brake() {
    if (_lastSpeedA == 0 && _lastSpeedB == 0) return; // already stationary, nothing to brake
    uint8_t brakeA = _clamp((int)_lastSpeedA * BRAKE_POWER_PERCENT / 100);
    uint8_t brakeB = _clamp((int)_lastSpeedB * BRAKE_POWER_PERCENT / 100);
    _motors.setInvertA(!_lastInvertA);
    _motors.setInvertB(!_lastInvertB);
    _motors.setSpeedA(brakeA);
    _motors.setSpeedB(brakeB);
    delay(BRAKE_PULSE_MS);
    _lastSpeedA = 0;
    _lastSpeedB = 0;
  }

  /// @brief Starts (or continues, if the same direction is already running) a live-corrected straight drive.
  /// Captures _idleStateRz fresh only if the robot was actually stationary just before this call (guarded by
  /// _lastSpeedA/_lastSpeedB == 0) -- otherwise a mid-turn rz would get latched as "idle" and the controller
  /// would then fight to maintain that turn. No settle/wait phase: this is an immediate capture, matching the
  /// issue's "when the go ahead operation starts, save the idle state's rz" -- not a calibration-style
  /// stabilization wait, which would add an unrequested hesitation before the robot starts moving.
  void _beginStraightRun(bool reversed) {
    bool sameRun = _straightActive && _straightReversed == reversed;
    if (!sameRun) _straightCorrection = 0;
    if ((_lastSpeedA == 0 && _lastSpeedB == 0) || !_idleStateRzCaptured) {
      float ax, ay, az, rx, ry, rz;
      _movement.get(&ax, &ay, &az, &rx, &ry, &rz);
      _idleStateRz = rz;
      _idleStateRzCaptured = true;
    }
    _straightActive = true;
    _straightReversed = reversed;
    _straightLastCorrectionAt = millis();
  }

  /// @brief Applies the current straight-line correction to the motors. forward = (true,true) invert,
  /// reverse = (false,false) -- inverted together so the same +correction/-correction pairing (A faster,
  /// B slower) is reused in both directions; _applyStraightDrive is what actually makes a correction take
  /// effect, called both when a straight run starts and whenever updateGoingStraight nudges the correction.
  void _applyStraightDrive() {
    bool inv = !_straightReversed;
    _drive(inv, inv, DRIVE_SPEED + _straightCorrection, DRIVE_SPEED - _straightCorrection);
  }

  /// @brief Looks up the target heading (degrees) for a "point_*" RPC command string. Returns false if the command isn't a point-to-bearing command.
  bool _bearingTargetDegrees(const String& command, float* degrees) {
    struct BearingEntry { const char* command; float degrees; };
    static const BearingEntry BEARINGS[] = {
      { "point_north",     0.0f },
      { "point_northeast", 45.0f },
      { "point_east",      90.0f },
      { "point_southeast", 135.0f },
      { "point_south",     180.0f },
      { "point_southwest", 225.0f },
      { "point_west",      270.0f },
      { "point_northwest", 315.0f },
    };
    for (unsigned int i = 0; i < sizeof(BEARINGS) / sizeof(BEARINGS[0]); i++) {
      if (command == BEARINGS[i].command) {
        *degrees = BEARINGS[i].degrees;
        return true;
      }
    }
    return false;
  }

  /// @brief Signed angular error (degrees) from the current calibrated heading to a target, normalized to (-180, 180].
  float _headingError(float targetDegrees) {
    float error = targetDegrees - _compass.getDirectionAngle();
    while (error > 180.0f) error -= 360.0f;
    while (error <= -180.0f) error += 360.0f;
    return error;
  }

  /// @brief Chooses the shorter-way turn direction for a signed heading error. turn_right increases the reported
  /// heading (confirmed empirically against real hardware), so a positive error turns right.
  const char* _turnCommandFor(float error) {
    return error >= 0 ? "turn_right" : "turn_left";
  }

  /// @brief Open-loop duration for a turn of the given magnitude, from the trapezoid area under the
  /// smoothed yaw curve: degrees = peak * (active - ramp/2 + stop/2) / 1000, solved for active. Direction-
  /// specific because the two directions measure meaningfully different peak rates on real hardware.
  /// Returns -1 when turn calibration hasn't run this boot -- callers fall back to the compass-threshold
  /// stopping trigger. Also returns -1 when the computed duration exceeds MAX_TURN_DURATION_MS -- an
  /// untrustworthy calibration (e.g. a peak dps just above TURN_CALIBRATED_MIN_DPS) could otherwise compute
  /// a very large duration, and _beginTurn's blocking delay() has no other way to be interrupted.
  int _computeTurnDurationMs(float errorDegrees, bool turningRight) {
    float peak, rampMs, stopMs;
    if (turningRight) {
      peak   = fabsf(_minimumRzWhenTurningRight - _idleRz1);
      rampMs = (float)_timeInMillisecondsToReachMinimumRzWhenTurningRight;
      stopMs = (float)_timeInMillisecondsToStopWhenTurningRight;
    } else {
      peak   = fabsf(_maximumRzWhenTurningLeft - _idleRz1);
      rampMs = (float)_timeInMillisecondsToReachMaximumRzWhenTurningLeft;
      stopMs = (float)_timeInMillisecondsToStopWhenTurningLeft;
    }
    if (peak < TURN_CALIBRATED_MIN_DPS) return -1;
    float ms = fabsf(errorDegrees) * 1000.0f / peak + 0.5f * rampMs - 0.5f * stopMs;
    if (ms < 0) ms = 0;
    if (ms > MAX_TURN_DURATION_MS) return -1; // untrustworthy for this turn size -- fall back rather than block unboundedly
    return (int)ms;
  }

  /// @brief Starts a turn toward the given heading error. When turn calibration is available the turn is
  /// executed as a precise, blocking open-loop pulse (command turn, delay() for the calculated duration,
  /// stop) -- a loop-polled cutoff is far too coarse at full turn rate (tens of degrees per loop iteration).
  /// This leaves _pointState in POINT_SETTLING when calibrated. Uncalibrated, it falls back to the existing
  /// compass-threshold/sign-flip triggers in updatePointing by leaving _pointState in POINT_TURNING.
  void _beginTurn(float error) {
    bool right = error >= 0; // matches _turnCommandFor()'s convention
    int durationMs = _computeTurnDurationMs(fabsf(error), right);
    move(right ? "turn_right" : "turn_left");
    _pointTurnStartedAt = millis();
    _pointTurnDurationMs = durationMs;
    if (durationMs < 0) {
      _pointState = POINT_TURNING; // uncalibrated fallback -- updatePointing's threshold/sign-flip triggers drive it
      return;
    }
    delay(durationMs);
    move("stop"); // blocks further (~320ms) for the brake pulse + settle delay
    _pointState = POINT_SETTLING;
    _pointSettleStartedAt = millis(); // fresh timestamp -- the caller's `now` is stale by durationMs+~320ms after this
    _pointSettleSamples = 0;
  }

  /// @brief Stops the point-to-bearing turn and reports the result.
  void _finishPointing(bool timedOut) {
    move("stop");
    _pointState = POINT_IDLE;
    float finalError = _headingError(_pointTargetDegrees);

    Monitor.println();
    Monitor.println("--- POINT TO BEARING ---");
    Monitor.print(_pointCommand); Monitor.print(" target="); Monitor.print(_pointTargetDegrees, 1);
    Monitor.print(" heading="); Monitor.print(_compass.getDirectionAngle(), 1);
    Monitor.print(" error="); Monitor.print(finalError, 1);
    Monitor.print(" timeout="); Monitor.println(timedOut ? "yes" : "no");
    Monitor.println("-------------------------");
    Monitor.flush();

    _display.print(timedOut ? "e4" : "ok");
  }

public:
  RobotMotors(ISmoothCompass& compass, ISmoothMovement& movement, ILedMatrixDisplay& display)
    : _compass(compass), _movement(movement), _display(display) {}
  virtual ~RobotMotors()
  {
  }

  bool initialize() override
  {
    _ok = _motors.begin();
    if (!_ok)
    {
      return false;
    }
    _motors.setStepperModeEnabled(false);
    return _ok;
  }

  bool isOk() override { return _ok; }

  // Motor A drives the left wheel, Motor B the right wheel.
  bool move(String command) override
  {
    _status = "NOK";
    if (!_ok) return false;

    if (command == "go_ahead")
    {
      _status = "GHD";
      _beginStraightRun(false);
      _applyStraightDrive();
    }
    else if (command == "go_back")
    {
      _status = "GBK";
      _beginStraightRun(true);
      _applyStraightDrive();
    }
    else if (command == "turn_right")
    {
      _status = "TRG";
      _straightActive = false;
      _drive(true, false, DRIVE_SPEED, DRIVE_SPEED);
    }
    else if (command == "turn_left")
    {
      _status = "TLF";
      _straightActive = false;
      _drive(false, true, DRIVE_SPEED, DRIVE_SPEED);
    }
    else if (command == "stop")
    {
      _status = "STP";
      _straightActive = false;
      _brake(); // already zeroes _lastSpeedA/B once the pulse is applied (or no-ops if already stopped)
      _motors.stop();
      delay(200);
      _motors.stop();
    }
    else
    {
      return false;
    }
    return true;
  }

  String getStatus() override
  {
    return _status;
  }

  uint8_t getDriveSpeed() override { return DRIVE_SPEED; }

  float getIdleRz1() override { return _idleRz1; }
  void setIdleRz1(float rz) override { _idleRz1 = rz; }
  float getMinimumRzWhenTurningRight() override { return _minimumRzWhenTurningRight; }
  void setMinimumRzWhenTurningRight(float rz) override { _minimumRzWhenTurningRight = rz; }
  int getTimeInMillisecondsToReachMinimumRzWhenTurningRight() override { return _timeInMillisecondsToReachMinimumRzWhenTurningRight; }
  void setTimeInMillisecondsToReachMinimumRzWhenTurningRight(int ms) override { _timeInMillisecondsToReachMinimumRzWhenTurningRight = ms; }
  int getTimeInMillisecondsToStopWhenTurningRight() override { return _timeInMillisecondsToStopWhenTurningRight; }
  void setTimeInMillisecondsToStopWhenTurningRight(int ms) override { _timeInMillisecondsToStopWhenTurningRight = ms; }
  float getMaximumRzWhenTurningLeft() override { return _maximumRzWhenTurningLeft; }
  void setMaximumRzWhenTurningLeft(float rz) override { _maximumRzWhenTurningLeft = rz; }
  int getTimeInMillisecondsToReachMaximumRzWhenTurningLeft() override { return _timeInMillisecondsToReachMaximumRzWhenTurningLeft; }
  void setTimeInMillisecondsToReachMaximumRzWhenTurningLeft(int ms) override { _timeInMillisecondsToReachMaximumRzWhenTurningLeft = ms; }
  int getTimeInMillisecondsToStopWhenTurningLeft() override { return _timeInMillisecondsToStopWhenTurningLeft; }
  void setTimeInMillisecondsToStopWhenTurningLeft(int ms) override { _timeInMillisecondsToStopWhenTurningLeft = ms; }

  /// @brief Whether the given RPC command string names a point-to-bearing target.
  bool isBearingCommand(const String& command) override {
    float d;
    return _bearingTargetDegrees(command, &d);
  }

  /// @brief Starts a closed-loop turn toward a target bearing, if the compass has been calibrated this boot and required sensors/motors are ready.
  bool startPointing(const String& command, bool compassCalibrated) override {
    float targetDegrees;
    if (!_bearingTargetDegrees(command, &targetDegrees)) return false; // not a bearing command -- shouldn't normally be reached, caller should check isBearingCommand first
    if (!(isOk() && _compass.isOk() && _movement.isOk())) {
      _display.print("e5");
      return false;
    }
    if (!compassCalibrated) {
      _display.print("e6");
      return false;
    }
    _pointCommand = command;
    _pointTargetDegrees = targetDegrees;
    _pointStartedAt = millis();
    _pointLastError = _headingError(targetDegrees);
    if (fabsf(_pointLastError) <= POINT_TOLERANCE_DEGREES) {
      // Already facing the target bearing: stop any prior motion and skip straight to settling instead of turning.
      move("stop");
      _pointState = POINT_SETTLING;
      _pointSettleStartedAt = millis();
      _pointSettleSamples = 0;
    } else {
      _beginTurn(_pointLastError);
    }
    return true;
  }

  /// @brief Advances a point-to-bearing turn via a turn/settle/re-check cycle (a single-pass stop would overshoot,
  /// since SmoothCompass::getDirectionAngle() lags the true heading while actively spinning), bounded by POINT_TIMEOUT_MS.
  /// @return true on the iteration the turn finished (via timeout or a successful heading recheck), false otherwise.
  bool updatePointing(int now) override {
    if (_pointState == POINT_IDLE) return false;

    if (now - _pointStartedAt >= POINT_TIMEOUT_MS) {
      _finishPointing(true);
      return true;
    }

    bool finished = false;
    if (_pointState == POINT_TURNING) {
      float error = _headingError(_pointTargetDegrees);
      // Guards against jumping past the tolerance band between loop iterations: a sign flip while the
      // previous error was still small means the turn just crossed the target.
      bool signFlipped = (error > 0) != (_pointLastError > 0) && fabsf(_pointLastError) < 90.0f;
      bool thresholdReached = fabsf(error) <= POINT_TOLERANCE_DEGREES;
      bool timedTurnDone = _pointTurnDurationMs >= 0 && (now - _pointTurnStartedAt) >= _pointTurnDurationMs;
      if (thresholdReached || signFlipped || timedTurnDone) {
        move("stop");
        _pointState = POINT_SETTLING;
        _pointSettleStartedAt = now;
        _pointSettleSamples = 0;
      }
      _pointLastError = error;
    } else if (_pointState == POINT_SETTLING) {
      _pointSettleSamples++;
      if (now - _pointSettleStartedAt >= POINT_SETTLE_MS && _pointSettleSamples >= _compass.getSampleCount()) {
        float error = _headingError(_pointTargetDegrees);
        if (fabsf(error) <= POINT_TOLERANCE_DEGREES) {
          _finishPointing(false);
          finished = true;
        } else {
          _pointLastError = error;
          _beginTurn(error);
        }
      }
    }
    return finished;
  }

  /// @brief Cancels an in-progress point-to-bearing turn without writing a result to the display (the incoming command's own code takes over).
  void cancelPointing() override {
    move("stop");
    _pointState = POINT_IDLE;
  }

  bool isPointing() override { return _pointState != POINT_IDLE; }
  bool isPointingAt(const String& command) override { return _pointState != POINT_IDLE && command == _pointCommand; }

  /// @brief Prints the point-to-bearing turn's target/current heading while one is in progress.
  void showPointingStatus() override {
    if (_pointState != POINT_IDLE) {
      Monitor.print(" PNT: ");
      Monitor.print(_pointCommand);
      Monitor.print(" target=");monitorPrintLeftJustified(_pointTargetDegrees, 6);
      Monitor.print(" err=");   monitorPrintLeftJustified(_headingError(_pointTargetDegrees), 6);
    }
  }

  /// @brief Live per-tick straight-line yaw-hold: every STRAIGHT_CORRECTION_INTERVAL_MS while a go_ahead/
  /// go_back run is active, nudges the per-wheel correction by one STRAIGHT_CORRECTION_STEP in whichever
  /// direction opposes the current deviation from _idleStateRz, if that deviation exceeds STRAIGHT_DEADBAND_DPS.
  /// An incremental (integral-action) nudge rather than a proportional map from rz to correction: the caster
  /// is a persistent disturbance, and a proportional law would leave a steady-state error (needing a nonzero
  /// rz to sustain the very correction that cancels it) -- this converges to and HOLDS the exact cancelling
  /// correction instead.
  void updateGoingStraight(int now) override {
    if (!_straightActive || !_ok || !_movement.isOk()) return;
    if (now - _straightLastCorrectionAt < STRAIGHT_CORRECTION_INTERVAL_MS) return;
    _straightLastCorrectionAt = now;

    float ax, ay, az, rx, ry, rz;
    _movement.get(&ax, &ay, &az, &rx, &ry, &rz);
    float deviation = rz - _idleStateRz;
    if (fabsf(deviation) < STRAIGHT_DEADBAND_DPS) return;

    // rz > 0 == counter-clockwise == drifting LEFT (turn_left = (false,true) invert = CCW, and
    // MotorCalibration records that direction as _maximumRzWhenTurningLeft, a positive value -- confirmed
    // against a real captured turn peaking at +148 dps). Driving forward, a POSITIVE correction speeds up
    // A (left) and slows B (right), curving the robot RIGHT -- which is what cancels a leftward (positive-rz)
    // drift. Reverse flips the mapping (both wheels' roles swap), hence `dir`.
    // IMPORTANT: verify this sign on the first real hardware run -- if the robot curves HARDER instead of
    // straightening out, flip this single ternary. Do not add a separate configurable sign constant for
    // this (the old RZ_POSITIVE_IS_RIGHT was exactly that pattern and shipped with the wrong sign).
    int dir = _straightReversed ? -1 : 1;
    int8_t next = (int8_t)constrain(
        (int)_straightCorrection + dir * (deviation > 0 ? STRAIGHT_CORRECTION_STEP : -STRAIGHT_CORRECTION_STEP),
        -(int)MAX_STRAIGHT_CORRECTION, (int)MAX_STRAIGHT_CORRECTION);
    if (next == _straightCorrection) return; // saturated -- no redundant I2C writes
    _straightCorrection = next;
    _applyStraightDrive();
  }

  /// @brief Prints the live straight-line correction's current deviation/correction while a go_ahead/go_back
  /// run is active -- the only way to verify the sign convention and watch convergence on real hardware.
  void showStraightStatus() override {
    if (!_straightActive) return;
    float ax, ay, az, rx, ry, rz;
    _movement.get(&ax, &ay, &az, &rx, &ry, &rz);
    Monitor.print(" STR: dev="); monitorPrintLeftJustified(rz - _idleStateRz, 6);
    Monitor.print(" cor="); Monitor.print(_straightCorrection);
  }
};

#endif
