#ifndef _MOTOR_CALIBRATION_
#define _MOTOR_CALIBRATION_ 1
#include <Arduino.h>
#include <Arduino_RouterBridge.h>
#include "IMotorCalibration.h"
#include "ISmoothMovement.h"
#include "IRobotMotors.h"
#include "ILedMatrixDisplay.h"
#include "MonitorFormat.h"

/// @brief Drives a motor-powered, gyro-closed-loop motor calibration: a fixed-speed right turn and a
/// fixed-speed left turn, each held for at least TURN_MIN_DURATION_MS, recording per-direction peak yaw
/// rate, time-to-peak, and time-to-full-stop into RobotMotors -- consumed there to compute precise
/// open-loop timed turns. Owns its full lifecycle: start/update/report/cancel, Monitor output, and LED
/// matrix display.
class MotorCalibration : public IMotorCalibration
{
public:
  enum Phase { IDLE, BASELINE, SETTLE,
               TURN_RIGHT_MEASURE, STOP_RIGHT_MEASURE,
               TURN_LEFT_MEASURE,  STOP_LEFT_MEASURE };

private:
  static constexpr int BASELINE_MS = 500;
  static constexpr int SETTLE_MS = 800;
  // BASELINE(500) + SETTLE(800+) + TURN_RIGHT_MEASURE(up to 8000+320) + STOP_RIGHT_MEASURE(up to 6000)
  // + TURN_LEFT_MEASURE(up to 8000+320) + STOP_LEFT_MEASURE(up to 6000) ~= 29,940ms worst case, plus margin --
  // SETTLE has no timeout of its own beyond SETTLE_MS, so this leaves ~15s of slack for it to overrun before
  // the whole run spuriously fails via this global timeout.
  static constexpr int TIMEOUT_MS = 45000;
  static constexpr int TURN_MIN_DURATION_MS = 4000;    // the issue's "at least 4 seconds"
  static constexpr int TURN_MAX_DURATION_MS = 8000;    // if the plateau hasn't latched by TURN_MIN_DURATION_MS, keep going up to here; still unlatched -> fail
  static constexpr int STOP_MEASURE_TIMEOUT_MS = 6000; // real measured stop/coast time on hardware is ~3s; this gives ~2x margin. Reaching this is a FAILURE (not a silent cap that corrupts the measurement -- this was the root cause of the previous calibration's failure).
  static constexpr int STOP_CONFIRM_MS = 200;          // same idiom as the old COAST_CONFIRM_MS
  // The issue's "5%" -- used ONLY for ramp-up plateau detection. On real hardware data, consecutive-read
  // percentage deltas during ramp-down stay well above 5% for nearly the whole descent (the absolute step
  // size is roughly constant while the current value shrinks, so the percentage relative to a shrinking
  // base doesn't drop) and become erratic near zero, so a "<5%" test doesn't reliably fire near actual idle.
  // An absolute deadband near idle (RZ_DEADBAND_DPS, matching the idiom used elsewhere in this class) is
  // what actually detects "stopped" -- see _trackStop.
  static constexpr float RZ_SIGNIFICANT_FRACTION = 0.05f;
  static constexpr float RZ_DEADBAND_DPS = 3.0f;      // settle-phase "has stopped rotating" threshold
  static constexpr float TURN_DETECT_DPS = 15.0f;     // "robot is now turning" threshold

  IRobotMotors& _motors;
  ISmoothMovement& _movement;
  ILedMatrixDisplay& _display;
  Phase _phase = IDLE;
  Phase _lastReportedPhase = IDLE;
  bool _lastResultOk = false;
  bool _lastTimedOut = false;
  int _startedAt = 0;
  int _phaseStartedAt = 0;
  float _rzBaseline = 0;
  float _idleRzForTurn = 0;
  int   _turnStartedAt = 0;      // fresh millis() taken right after the turn command was issued
  int   _stopStartedAt = 0;      // fresh millis() taken right after the blocking move("stop") returned
  int   _stopQuietSince = 0;     // first instant rz entered the idle deadband this stop-measure phase; 0 = not in band
  float _turnExtremeRz = 0;      // running min (right turn) / max (left turn) over the whole turn-measure phase
  float _turnPrevRz = 0;         // previous rz read, for the 5% consecutive-delta plateau test
  bool  _plateauLatched = false;
  int   _plateauReachedAt = 0;   // millis() at the latch instant
  int   _turnSampleCount = 0;    // diagnostic: number of _step() calls from turn start to the plateau latch -- printed in the finish report so the operator can sanity-check the calibration model on real hardware

  void _finish(bool ok, bool timedOut) {
    _motors.move("stop");
    _phase = IDLE;
    _lastResultOk = ok;
    _lastTimedOut = timedOut;
  }

  /// @brief Enters a turn-measure phase: issues the turn through the normal move() path (same speed/bias a
  /// real point-to-bearing turn will use) and resets the tracking state.
  void _beginTurnMeasure(bool right, float rz) {
    _motors.move(right ? "turn_right" : "turn_left");
    int afterStart = millis();
    _turnStartedAt = afterStart;
    _turnExtremeRz = rz;
    _turnPrevRz = rz;
    _plateauLatched = false;
    _plateauReachedAt = 0;
    _turnSampleCount = 0;
  }

  /// @brief Per-iteration tracking for a turn-measure phase: running extreme value + 5% consecutive-delta
  /// plateau latch. Returns true once the turn should end (plateau latched AND at least TURN_MIN_DURATION_MS
  /// elapsed).
  bool _trackTurn(int now, float rz, bool right) {
    _turnSampleCount++;
    if (right) { if (rz < _turnExtremeRz) _turnExtremeRz = rz; }
    else       { if (rz > _turnExtremeRz) _turnExtremeRz = rz; }

    bool moving = fabsf(rz - _idleRzForTurn) > TURN_DETECT_DPS; // guards the 5% test against firing while still near idle
    if (!_plateauLatched && moving) {
      float delta = fabsf(rz - _turnPrevRz);
      if (delta < RZ_SIGNIFICANT_FRACTION * fabsf(rz)) {
        _plateauLatched = true;
        _plateauReachedAt = now;
      }
    }
    _turnPrevRz = rz;

    int elapsed = now - _turnStartedAt;
    return _plateauLatched && elapsed >= TURN_MIN_DURATION_MS;
  }

  /// @brief Enters a stop-measure phase right after a blocking move("stop") call. Must be called with a
  /// FRESHLY-taken millis() value (see comment at the RATE_MEASURE->COAST_MEASURE transition this replaces) --
  /// never the stale `now` parameter from before the blocking call.
  void _beginStopMeasure(int afterStop) {
    _stopStartedAt = afterStop;
    _stopQuietSince = 0;
  }

  /// @brief Per-iteration tracking for a stop-measure phase: an absolute idle-deadband check (NOT the 5% rule
  /// -- see class doc comment for why), sustained for STOP_CONFIRM_MS. Returns true once confirmed stopped.
  bool _trackStop(int now, float rz) {
    bool quiet = fabsf(rz - _idleRzForTurn) < RZ_DEADBAND_DPS;
    if (quiet) { if (_stopQuietSince == 0) _stopQuietSince = now; }
    else       { _stopQuietSince = 0; }
    return _stopQuietSince != 0 && (now - _stopQuietSince) >= STOP_CONFIRM_MS;
  }

  // Feeds the current smoothed rz (deg/s) into the state machine. Returns true the iteration the whole
  // chain (turns) finishes, whether successfully or not.
  bool _step(int now, float rz) {
    if (_phase == IDLE) return false;

    if (now - _startedAt >= TIMEOUT_MS) {
      _finish(false, true);
      return true;
    }

    switch (_phase) {
      case BASELINE:
        if (now - _phaseStartedAt >= BASELINE_MS) {
          _rzBaseline = rz;
          _phase = SETTLE;
          _phaseStartedAt = now;
        }
        break;

      case SETTLE:
        if (now - _phaseStartedAt >= SETTLE_MS && fabsf(rz - _rzBaseline) < RZ_DEADBAND_DPS) {
          _rzBaseline = rz;
          _idleRzForTurn = rz;
          _motors.setIdleRz1(rz); // the issue's "Record the idle rz into _idleRz1"
          _beginTurnMeasure(true, rz);
          _phase = TURN_RIGHT_MEASURE;
        }
        break;

      case TURN_RIGHT_MEASURE:
        if (_trackTurn(now, rz, true)) {
          _motors.setMinimumRzWhenTurningRight(_turnExtremeRz);
          _motors.setTimeInMillisecondsToReachMinimumRzWhenTurningRight(_plateauReachedAt - _turnStartedAt);
          _motors.move("stop"); // blocks ~320ms
          _beginStopMeasure(millis()); // fresh timestamp taken AFTER the blocking call returns
          _phase = STOP_RIGHT_MEASURE;
        } else if (now - _turnStartedAt >= TURN_MAX_DURATION_MS) {
          _finish(false, false); // never plateaued within the max window -- no trustworthy ramp time
          return true;
        }
        break;

      case STOP_RIGHT_MEASURE:
        if (_trackStop(now, rz)) {
          _motors.setTimeInMillisecondsToStopWhenTurningRight(_stopQuietSince - _stopStartedAt);
          _rzBaseline = rz;
          _beginTurnMeasure(false, rz);
          _phase = TURN_LEFT_MEASURE;
        } else if (now - _stopStartedAt >= STOP_MEASURE_TIMEOUT_MS) {
          _finish(false, false); // reaching the timeout is a genuine failure, not a silent measurement cap
          return true;
        }
        break;

      case TURN_LEFT_MEASURE:
        if (_trackTurn(now, rz, false)) {
          _motors.setMaximumRzWhenTurningLeft(_turnExtremeRz);
          _motors.setTimeInMillisecondsToReachMaximumRzWhenTurningLeft(_plateauReachedAt - _turnStartedAt);
          _motors.move("stop");
          _beginStopMeasure(millis());
          _phase = STOP_LEFT_MEASURE;
        } else if (now - _turnStartedAt >= TURN_MAX_DURATION_MS) {
          _finish(false, false);
          return true;
        }
        break;

      case STOP_LEFT_MEASURE:
        if (_trackStop(now, rz)) {
          _motors.setTimeInMillisecondsToStopWhenTurningLeft(_stopQuietSince - _stopStartedAt);
          _finish(true, false);
          return true;
        } else if (now - _stopStartedAt >= STOP_MEASURE_TIMEOUT_MS) {
          _finish(false, false);
          return true;
        }
        break;

      default:
        break;
    }
    return false;
  }

  /// @brief Reports the just-finished motor calibration run's result to the Monitor and LED matrix.
  void _reportFinished() {
    bool ok = getLastResultOk();
    bool timedOut = getLastTimedOut();
    Monitor.println();
    Monitor.println("--- FINISH MOTOR CALIBRATION ---");
    Monitor.print("timeout="); Monitor.print(timedOut ? "yes" : "no");
    Monitor.print("  ok="); Monitor.println(ok ? "yes" : "no");
    Monitor.print("idle_rz="); Monitor.println(_motors.getIdleRz1(), 2);
    Monitor.print("right: min_rz="); Monitor.print(_motors.getMinimumRzWhenTurningRight(), 2);
    Monitor.print("  ramp_ms="); Monitor.print(_motors.getTimeInMillisecondsToReachMinimumRzWhenTurningRight());
    Monitor.print("  stop_ms="); Monitor.println(_motors.getTimeInMillisecondsToStopWhenTurningRight());
    Monitor.print("left:  max_rz="); Monitor.print(_motors.getMaximumRzWhenTurningLeft(), 2);
    Monitor.print("  ramp_ms="); Monitor.print(_motors.getTimeInMillisecondsToReachMaximumRzWhenTurningLeft());
    Monitor.print("  stop_ms="); Monitor.println(_motors.getTimeInMillisecondsToStopWhenTurningLeft());
    Monitor.print("turn_samples_to_plateau="); Monitor.println(_turnSampleCount);
    Monitor.println("---------------------------");
    Monitor.flush();
    _display.print(ok ? "rdy" : "e8");
  }

public:
  MotorCalibration(IRobotMotors& motors, ISmoothMovement& movement, ILedMatrixDisplay& display)
    : _motors(motors), _movement(movement), _display(display) {}

  /// @brief Whether a calibration run is currently in progress.
  bool isActive() override { return _phase != IDLE; }
  /// @brief Current phase of an in-progress (or just-finished) calibration run.
  Phase getPhase() { return _phase; }
  /// @brief Whether the most recently finished run succeeded.
  bool getLastResultOk() { return _lastResultOk; }
  /// @brief Whether the most recently finished run ended via timeout rather than completing normally.
  bool getLastTimedOut() { return _lastTimedOut; }

  /// @brief Starts a motor-driven calibration run, if the required sensors and motors are ready.
  bool start() override {
    if (!(_movement.isOk() && _motors.isOk())) {
      _display.print("e7");
      return false;
    }
    Monitor.println();
    Monitor.println("--- STARTING MOTOR CALIBRATION ---");
    Monitor.println();
    _phase = BASELINE;
    _startedAt = _phaseStartedAt = millis();
    _motors.move("stop");
    return true;
  }

  /// @brief Cancels an in-progress calibration run without applying/finishing a result.
  void cancel() override {
    _motors.move("stop");
    _phase = IDLE;
    _lastReportedPhase = IDLE;
  }

  /// @brief Feeds the current gyro rate into the motor calibration state machine, drives its phase-transition
  /// LED updates, and reports when it finishes.
  bool update(int now) override {
    if (_phase == IDLE && _lastReportedPhase == IDLE) return false;
    if (_phase != _lastReportedPhase) {
      if (_phase == BASELINE)               _display.print("cms");
      else if (_phase == TURN_RIGHT_MEASURE) _display.print("cmt");
      else if (_phase == TURN_LEFT_MEASURE)  _display.print("cml");
      _lastReportedPhase = _phase;
    }
    float ax=0,ay=0,az=0,rx=0,ry=0,rz=0;
    _movement.get(&ax, &ay, &az, &rx, &ry, &rz);
    if (_step(now, rz)) {
      _lastReportedPhase = IDLE;
      _reportFinished();
      return true;
    }
    return false;
  }

  /// @brief Prints the motor calibration run's live tracked extreme rz while it is in progress.
  void showStatus() override {
    if (isActive()) {
      Monitor.print(" MCL: rz="); monitorPrintLeftJustified(_turnExtremeRz, 6);
    }
  }
};
#endif
