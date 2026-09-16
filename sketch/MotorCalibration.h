#ifndef _MOTOR_CALIBRATION_
#define _MOTOR_CALIBRATION_ 1
#include <Arduino.h>
#include "RobotMotors.h"

/// @brief Drives a motor-powered, gyro-closed-loop motor calibration: first a straight-line run that
/// integrates the IMU's rz to measure how much the robot veers (correcting it via RobotMotors' per-wheel
/// straight bias), then a turn-power ramp that finds the lowest turn power that reliably sustains rotation
/// (stored back into RobotMotors' turn speed). Pure state/logic, matching CompassCalibration's convention:
/// no Monitor output and no RPC/display knowledge -- the caller reads its getters to report status.
class MotorCalibration
{
public:
  enum Phase { IDLE, BASELINE, STRAIGHT, SETTLE, TURN_RAMP };

private:
  static constexpr int BASELINE_MS = 500;
  static constexpr int STRAIGHT_MS = 5000;
  static constexpr int SETTLE_MS = 800;
  static constexpr int RAMP_STEP_MS = 600;
  static constexpr int TIMEOUT_MS = 20000;
  static constexpr float RZ_DEADBAND_DPS = 3.0f;      // settle-phase "has stopped rotating" threshold
  static constexpr float TURN_DETECT_DPS = 15.0f;     // ramp "robot is now turning" threshold
  static constexpr float BIAS_UNITS_PER_DEGREE = 0.2f;
  static constexpr float MIN_DRIFT_DEGREES = 3.0f;    // below this, treat as already straight
  static constexpr float RZ_POSITIVE_IS_RIGHT = 1.0f; // sign convention; flip to -1.0f if hardware verification shows it's backwards
  static const uint8_t RAMP_START_POWER_DIVISOR = 2;  // ramp starts at DRIVE_SPEED/2, per "half the power used for moving straight"
  static const uint8_t TURN_POWER_STEP = 5;
  static const uint8_t TURN_SPEED_MARGIN = 5;         // extra power above the detected threshold, so the calibrated turn power reliably sustains motion, not just starts it

  RobotMotors& _motors;
  Phase _phase = IDLE;
  bool _lastResultOk = false;
  bool _lastTimedOut = false;
  int _startedAt = 0;
  int _phaseStartedAt = 0;
  int _stepStartedAt = 0;
  int _lastTick = 0;
  float _rzBaseline = 0;
  float _driftDegrees = 0;
  uint8_t _rampPower = 0;

  void _finish(bool ok, bool timedOut) {
    _motors.move("stop");
    _phase = IDLE;
    _lastResultOk = ok;
    _lastTimedOut = timedOut;
  }

public:
  MotorCalibration(RobotMotors& motors) : _motors(motors) {}

  /// @brief Whether a calibration run is currently in progress.
  bool isActive() { return _phase != IDLE; }
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

  /// @brief Starts a motor-driven calibration run. Caller is responsible for checking prerequisite sensors/motors
  /// are ready before calling this.
  void start() {
    _phase = BASELINE;
    _startedAt = _phaseStartedAt = _lastTick = millis();
    _driftDegrees = 0;
    _motors.move("stop");
  }

  /// @brief Cancels an in-progress calibration run without applying/finishing a result.
  void cancel() {
    _motors.move("stop");
    _phase = IDLE;
  }

  // Feeds the current smoothed rz (deg/s) into the state machine. Returns true the iteration the whole
  // chain (straight-line + turn) finishes, whether successfully or not.
  bool update(int now, float rz) {
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
          _rampPower = RobotMotors::getDriveSpeed() / RAMP_START_POWER_DIVISOR;
          _motors.turnAtSpeed("turn_right", _rampPower);
          _stepStartedAt = now;
          _phase = TURN_RAMP;
        }
        break;

      case TURN_RAMP:
        if (now - _stepStartedAt >= RAMP_STEP_MS) {
          if (fabsf(rz - _rzBaseline) > TURN_DETECT_DPS) {
            _motors.setTurnSpeed(_rampPower + TURN_SPEED_MARGIN);
            _finish(true, false);
            return true;
          }
          _rampPower += TURN_POWER_STEP;
          if (_rampPower > 100) {
            _finish(false, false);
            return true;
          }
          _motors.turnAtSpeed("turn_right", _rampPower);
          _stepStartedAt = now;
        }
        break;

      default:
        break;
    }
    return false;
  }
};
#endif
