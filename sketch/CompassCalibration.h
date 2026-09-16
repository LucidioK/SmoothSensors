#ifndef _COMPASS_CALIBRATION_
#define _COMPASS_CALIBRATION_ 1
#include <Arduino.h>
#include "SmoothCompass.h"
#include "RobotMotors.h"

/// @brief Drives a motor-powered, gyro-closed-loop compass calibration spin: turns the robot in place,
/// integrating the IMU's rz to track how far it has rotated, and stops once it has swept far enough (with a
/// hard timeout as a safety net against a stuck/lifted robot) -- then hands the result to SmoothCompass to
/// compute the hard-/soft-iron correction. Pure state/logic, matching SmoothCompass/SmoothDistance/SmoothMovement's
/// convention: no Monitor output and no RPC/display knowledge -- the caller reads its getters to report status.
class CompassCalibration
{
private:
  // Cumulative rotation (degrees) at which the spin stops; overshoots 360 to absorb gyro lag/undershoot.
  static constexpr float TARGET_DEGREES = 380.0f;
  // Hard timeout, guarding against the robot being stuck or lifted mid-spin. Turns now run at the
  // calibrated (potentially slower) turn power via RobotMotors, so the spin needs more time to complete.
  static constexpr int TIMEOUT_MS = 20000;
  // Gyro Z-axis rate below which rotation is treated as zero-bias noise rather than real motion.
  static constexpr float RZ_DEADBAND_DPS = 3.0f;

  SmoothCompass& _compass;
  RobotMotors& _motors;
  bool _active = false;
  bool _succeededOnce = false;
  bool _lastResultOk = false;
  bool _lastTimedOut = false;
  int _startedAt = 0;
  int _lastTick = 0;
  float _degrees = 0;

  void _finish(bool timedOut) {
    _motors.move("stop");
    _active = false;
    _lastTimedOut = timedOut;
    if (_degrees >= MIN_VALID_DEGREES) {
      _lastResultOk = _compass.finishCalibration();
      if (_lastResultOk) _succeededOnce = true;
    } else {
      _compass.cancelCalibration();
      _lastResultOk = false;
    }
  }

public:
  // Minimum accumulated rotation (degrees) for a spin to be considered valid; guards against accepting a spin
  // that barely moved (e.g. stuck/lifted) just because the raw magnetic span happened to clear SmoothCompass's
  // own (much smaller) noise-floor check. Public: callers need it to explain an "insufficient rotation" result.
  static constexpr float MIN_VALID_DEGREES = 300.0f;

  CompassCalibration(SmoothCompass& compass, RobotMotors& motors)
    : _compass(compass), _motors(motors) {}

  /// @brief Whether a spin is currently in progress.
  bool isActive() { return _active; }
  /// @brief Whether a calibration has succeeded at least once since this object was created (i.e. since boot).
  bool hasSucceededOnce() { return _succeededOnce; }
  /// @brief Cumulative rotation, in degrees, accumulated during the current (or most recently finished) spin.
  float getDegrees() { return _degrees; }
  /// @brief Whether the most recently finished spin succeeded.
  bool getLastResultOk() { return _lastResultOk; }
  /// @brief Whether the most recently finished spin ended via timeout rather than reaching the target angle.
  bool getLastTimedOut() { return _lastTimedOut; }

  /// @brief Starts a motor-driven calibration spin. Caller is responsible for checking prerequisite sensors/motors
  /// are ready before calling this.
  void start() {
    _compass.startCalibration();
    _degrees = 0;
    _startedAt = _lastTick = millis();
    _active = true;
    _motors.move("turn_right");
  }

  /// @brief Cancels an in-progress spin without computing/applying a result.
  void cancel() {
    _motors.move("stop");
    _active = false;
    _compass.cancelCalibration();
  }

  /// @brief Integrates gyro rotation and ends the spin once the target angle or timeout is reached.
  /// @return true if the spin finished during this call -- caller should react, e.g. read
  /// getLastResultOk()/getLastTimedOut() to report status.
  bool update(int now, float rz) {
    if (!_active) return false;
    int dt = now - _lastTick;
    _lastTick = now;
    float rate = fabsf(rz);
    // Below this rate, treat rz as gyro zero-bias noise rather than real rotation, to avoid slowly accumulating phantom degrees while stationary.
    if (rate > RZ_DEADBAND_DPS) {
      _degrees += rate * dt / 1000.0f;
    }
    if (_degrees >= TARGET_DEGREES) {
      _finish(false);
      return true;
    }
    if (now - _startedAt >= TIMEOUT_MS) {
      _finish(true);
      return true;
    }
    return false;
  }
};

#endif
