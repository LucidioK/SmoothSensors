#ifndef _MOTOR_CALIBRATION_
#define _MOTOR_CALIBRATION_ 1
#include <Arduino.h>
#include <Arduino_RouterBridge.h>
#include "IMotorCalibration.h"
#include "ISmoothMovement.h"
#include "IRobotMotors.h"
#include "ILedMatrixDisplay.h"
#include "MonitorFormat.h"

/// @brief Drives a motor-powered, gyro-closed-loop motor calibration: first a straight-line run that
/// integrates the IMU's rz to measure how much the robot veers (correcting it via RobotMotors' per-wheel
/// straight bias), then a turn-power ramp that finds the lowest turn power that reliably sustains rotation
/// (stored back into RobotMotors' turn speed), then a rate-measure phase and a coast-measure phase that
/// capture the steady-state turn rate and momentum coast angle at that finalized speed. Owns its full
/// lifecycle: start/update/report/cancel, Monitor output, and LED matrix display.
class MotorCalibration : public IMotorCalibration
{
public:
  enum Phase { IDLE, BASELINE, STRAIGHT, SETTLE, TURN_RAMP, RATE_MEASURE, COAST_MEASURE };

private:
  static constexpr int BASELINE_MS = 500;
  static constexpr int STRAIGHT_MS = 5000;
  static constexpr int SETTLE_MS = 800;
  static constexpr int RAMP_STEP_MS = 600;
  static constexpr int TIMEOUT_MS = 26000; // covers the two new phases -- BASELINE(500)+STRAIGHT(5000)+SETTLE(800+)+TURN_RAMP(worst case ~7200)+RATE_MEASURE(1000)+COAST_MEASURE(up to 2000) plus margin
  static constexpr int RATE_MEASURE_MS = 1000;        // time to run at the finalized turn speed before sampling the steady-state rate
  static constexpr int COAST_CONFIRM_MS = 200;         // gyro must stay quiet this long before declaring the coast-down finished
  static constexpr int COAST_MEASURE_TIMEOUT_MS = 2000; // safety cap in case the gyro never quiets down (e.g. sensor noise)
  static constexpr float RZ_DEADBAND_DPS = 3.0f;      // settle-phase "has stopped rotating" threshold
  static constexpr float TURN_DETECT_DPS = 15.0f;     // ramp "robot is now turning" threshold
  static constexpr float BIAS_UNITS_PER_DEGREE = 0.2f;
  static constexpr float MIN_DRIFT_DEGREES = 3.0f;    // below this, treat as already straight
  static constexpr float RZ_POSITIVE_IS_RIGHT = 1.0f; // sign convention; flip to -1.0f if hardware verification shows it's backwards
  static const uint8_t RAMP_START_POWER_DIVISOR = 2;  // ramp starts at DRIVE_SPEED/2, per "half the power used for moving straight"
  static const uint8_t TURN_POWER_STEP = 5;
  static const uint8_t TURN_SPEED_MARGIN = 5;         // extra power above the detected threshold, so the calibrated turn power reliably sustains motion, not just starts it

  IRobotMotors& _motors;
  ISmoothMovement& _movement;
  ILedMatrixDisplay& _display;
  Phase _phase = IDLE;
  Phase _lastReportedPhase = IDLE;
  bool _lastResultOk = false;
  bool _lastTimedOut = false;
  int _startedAt = 0;
  int _phaseStartedAt = 0;
  int _stepStartedAt = 0;
  int _lastTick = 0;
  float _rzBaseline = 0;
  float _driftDegrees = 0;
  uint8_t _rampPower = 0;
  int _rateMeasureStartedAt = 0;
  int _coastMeasureStartedAt = 0;
  int _coastQuietSince = 0;
  float _coastAccumDegrees = 0;

  void _finish(bool ok, bool timedOut) {
    _motors.move("stop");
    _phase = IDLE;
    _lastResultOk = ok;
    _lastTimedOut = timedOut;
  }

  // Feeds the current smoothed rz (deg/s) into the state machine. Returns true the iteration the whole
  // chain (straight-line + turn) finishes, whether successfully or not.
  bool _step(int now, float rz) {
    if (_phase == IDLE) return false;
    int dt = now - _lastTick;
    _lastTick = now;

    if (now - _startedAt >= TIMEOUT_MS) {
      _finish(false, true);
      return true;
    }

    switch (_phase) {
      case BASELINE:
        if (now - _phaseStartedAt >= BASELINE_MS) {
          _rzBaseline = rz;
          _phase = STRAIGHT;
          _phaseStartedAt = now;
          _motors.move("go_ahead");
        }
        break;

      case STRAIGHT:
        _driftDegrees += (rz - _rzBaseline) * dt / 1000.0f;
        if (now - _phaseStartedAt >= STRAIGHT_MS) {
          _motors.move("stop");
          int delta = 0;
          if (fabsf(_driftDegrees) >= MIN_DRIFT_DEGREES) {
            // delta is the corrective adjustment, opposite sign from the measured drift
            delta = -(int)roundf(BIAS_UNITS_PER_DEGREE * _driftDegrees * RZ_POSITIVE_IS_RIGHT);
          }
          int newBias = (int)_motors.getStraightBias() + delta;
          newBias = constrain(newBias, -127, 127);
          _motors.setStraightBias((int8_t)newBias); // setStraightBias itself clamps to +-MAX_STRAIGHT_BIAS
          _phase = SETTLE;
          _phaseStartedAt = now;
        }
        break;

      case SETTLE:
        if (now - _phaseStartedAt >= SETTLE_MS && fabsf(rz - _rzBaseline) < RZ_DEADBAND_DPS) {
          _rzBaseline = rz;
          _rampPower = _motors.getDriveSpeed() / RAMP_START_POWER_DIVISOR;
          _motors.turnAtSpeed("turn_right", _rampPower);
          _stepStartedAt = now;
          _phase = TURN_RAMP;
        }
        break;

      case TURN_RAMP:
        if (now - _stepStartedAt >= RAMP_STEP_MS) {
          if (fabsf(rz - _rzBaseline) > TURN_DETECT_DPS) {
            _motors.setTurnSpeed(_rampPower + TURN_SPEED_MARGIN);
            _motors.turnAtSpeed("turn_right", _motors.getTurnSpeed());
            _rateMeasureStartedAt = now;
            _phase = RATE_MEASURE;
          } else {
            _rampPower += TURN_POWER_STEP;
            if (_rampPower > 100) {
              _finish(false, false);
              return true;
            }
            _motors.turnAtSpeed("turn_right", _rampPower);
            _stepStartedAt = now;
          }
        }
        break;

      case RATE_MEASURE:
        if (now - _rateMeasureStartedAt >= RATE_MEASURE_MS) {
          // rz is already the smoothed (trimmed-mean) reading from SmoothMovement; after RATE_MEASURE_MS of
          // sustained turning at the finalized speed the buffer is fully refreshed, so this one sample is
          // effectively the steady-state rate.
          _motors.setTurnRateDps(fabsf(rz));
          _motors.move("stop"); // blocks for the brake pulse + settle delay (~320ms) -- re-stamp timing below using
                                 // a fresh millis() read taken AFTER this returns, not the stale `now` parameter,
                                 // so the next update() call's dt doesn't misattribute the blocked time as coast.
          int afterStop = millis();
          _lastTick = afterStop;
          _coastAccumDegrees = 0;
          _coastQuietSince = 0;
          _coastMeasureStartedAt = afterStop;
          _phase = COAST_MEASURE;
        }
        break;

      case COAST_MEASURE: {
        _coastAccumDegrees += fabsf(rz - _rzBaseline) * dt / 1000.0f;
        bool quiet = fabsf(rz - _rzBaseline) < RZ_DEADBAND_DPS;
        if (quiet) {
          if (_coastQuietSince == 0) _coastQuietSince = now;
        } else {
          _coastQuietSince = 0;
        }
        bool settled = _coastQuietSince != 0 && (now - _coastQuietSince) >= COAST_CONFIRM_MS;
        bool coastTimedOut = (now - _coastMeasureStartedAt) >= COAST_MEASURE_TIMEOUT_MS;
        if (settled || coastTimedOut) {
          _motors.setTurnCoastDegrees(_coastAccumDegrees);
          _finish(true, false);
          return true;
        }
        break;
      }

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
    Monitor.print("drift="); Monitor.print(getDriftDegrees(), 2);
    Monitor.print("deg  ramp_power="); Monitor.print(getRampPower());
    Monitor.print("  timeout="); Monitor.print(timedOut ? "yes" : "no");
    Monitor.print("  ok="); Monitor.println(ok ? "yes" : "no");
    Monitor.print("straight_bias="); Monitor.print(_motors.getStraightBias());
    Monitor.print("  turn_speed="); Monitor.println(_motors.getTurnSpeed());
    Monitor.print("turn_rate_dps="); Monitor.print(_motors.getTurnRateDps(), 2);
    Monitor.print("  turn_coast_deg="); Monitor.println(_motors.getTurnCoastDegrees(), 2);
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
  /// @brief Cumulative rz-integrated drift, in degrees, accumulated during the straight-line phase.
  float getDriftDegrees() { return _driftDegrees; }
  /// @brief Current (or final) turn power used by the turn-ramp phase.
  uint8_t getRampPower() { return _rampPower; }
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
    _startedAt = _phaseStartedAt = _lastTick = millis();
    _driftDegrees = 0;
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
      if (_phase == BASELINE)       _display.print("cms");
      else if (_phase == TURN_RAMP) _display.print("cmt");
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

  /// @brief Prints the motor calibration run's drift/ramp power while it is in progress.
  void showStatus() override {
    if (isActive()) {
      Monitor.print(" MCL: drift=");
      monitorPrintLeftJustified(getDriftDegrees(), 6);
      Monitor.print(" pwr="); monitorPrintLeftJustified((int)getRampPower(), 3);
    }
  }
};
#endif
