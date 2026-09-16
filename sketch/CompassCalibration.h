#ifndef _COMPASS_CALIBRATION_
#define _COMPASS_CALIBRATION_ 1
#include <Arduino.h>
#include <Arduino_RouterBridge.h>
#include "ICompassCalibration.h"
#include "ISmoothCompass.h"
#include "ISmoothMovement.h"
#include "IRobotMotors.h"
#include "ILedMatrixDisplay.h"
#include "MonitorFormat.h"

/// @brief Drives a motor-powered, gyro-closed-loop compass calibration spin: turns the robot in place,
/// integrating the IMU's rz to track how far it has rotated, and stops once it has swept far enough (with a
/// hard timeout as a safety net against a stuck/lifted robot) -- then hands the result to SmoothCompass to
/// compute the hard-/soft-iron correction. Owns its full lifecycle: start/update/report/cancel, Monitor
/// output, and LED matrix display.
class CompassCalibration : public ICompassCalibration
{
private:
  // Cumulative rotation (degrees) at which the spin stops; overshoots 360 to absorb gyro lag/undershoot.
  static constexpr float TARGET_DEGREES = 380.0f;
  // Hard timeout, guarding against the robot being stuck or lifted mid-spin. Turns now run at the
  // calibrated (potentially slower) turn power via RobotMotors, so the spin needs more time to complete.
  static constexpr int TIMEOUT_MS = 20000;
  // Gyro Z-axis rate below which rotation is treated as zero-bias noise rather than real motion.
  static constexpr float RZ_DEADBAND_DPS = 3.0f;

  ISmoothCompass& _compass;
  ISmoothMovement& _movement;
  IRobotMotors& _motors;
  ILedMatrixDisplay& _display;
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

  /// @brief Integrates gyro rotation and ends the spin once the target angle or timeout is reached.
  /// @return true if the spin finished during this call -- caller should react, e.g. read
  /// getLastResultOk()/getLastTimedOut() to report status.
  bool _step(int now, float rz) {
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

  /// @brief Reports the just-finished calibration spin's result to the Monitor and LED matrix.
  void _reportFinished() {
    bool ok = getLastResultOk();
    bool timedOut = getLastTimedOut();
    float degrees = getDegrees();

    Monitor.println();
    Monitor.println("--- FINISH COMPASS CALIBRATION ---");
    Monitor.print("spin="); Monitor.print(degrees, 2);
    Monitor.print(" deg  timeout="); Monitor.print(timedOut ? "yes" : "no");
    Monitor.print("  samples_ok="); Monitor.println(ok ? "yes" : "no");
    if (timedOut && degrees < 360.0f) {
      Monitor.println("WARNING: partial arc, constants unreliable");
    }
    if (ok) {
      Monitor.print("offset_x="); Monitor.print(_compass.getOffsetX(), 4);
      Monitor.print(" offset_y="); Monitor.println(_compass.getOffsetY(), 4);
      Monitor.print("scale_x="); Monitor.print(_compass.getScaleX(), 4);
      Monitor.print(" scale_y="); Monitor.print(_compass.getScaleY(), 4);
      Monitor.print("  radius="); Monitor.println(_compass.getCalibrationRadius(), 4);
      Monitor.println("Paste into SmoothCompass.h:");
      Monitor.print("  DEFAULT_OFFSET_X = "); Monitor.print(_compass.getOffsetX(), 4); Monitor.println("f;");
      Monitor.print("  DEFAULT_OFFSET_Y = "); Monitor.print(_compass.getOffsetY(), 4); Monitor.println("f;");
      Monitor.print("  DEFAULT_SCALE_X = "); Monitor.print(_compass.getScaleX(), 4); Monitor.println("f;");
      Monitor.print("  DEFAULT_SCALE_Y = "); Monitor.print(_compass.getScaleY(), 4); Monitor.println("f;");
    } else if (degrees < MIN_VALID_DEGREES) {
      Monitor.print("insufficient rotation: "); Monitor.print(degrees, 1);
      Monitor.print(" deg, need >= "); Monitor.println(MIN_VALID_DEGREES, 1);
    } else {
      Monitor.print("Calibration error: "); Monitor.println(_compass.getError());
    }
    Monitor.println("---------------------------");
    Monitor.flush();

    _display.print(ok ? "cal" : "e3");
  }

public:
  // Minimum accumulated rotation (degrees) for a spin to be considered valid; guards against accepting a spin
  // that barely moved (e.g. stuck/lifted) just because the raw magnetic span happened to clear SmoothCompass's
  // own (much smaller) noise-floor check. Public: callers need it to explain an "insufficient rotation" result.
  static constexpr float MIN_VALID_DEGREES = 300.0f;

  CompassCalibration(ISmoothCompass& compass, ISmoothMovement& movement, IRobotMotors& motors, ILedMatrixDisplay& display)
    : _compass(compass), _movement(movement), _motors(motors), _display(display) {}

  /// @brief Whether a spin is currently in progress.
  bool isActive() override { return _active; }
  /// @brief Whether a calibration has succeeded at least once since this object was created (i.e. since boot).
  bool hasSucceededOnce() override { return _succeededOnce; }
  /// @brief Cumulative rotation, in degrees, accumulated during the current (or most recently finished) spin.
  float getDegrees() { return _degrees; }
  /// @brief Whether the most recently finished spin succeeded.
  bool getLastResultOk() { return _lastResultOk; }
  /// @brief Whether the most recently finished spin ended via timeout rather than reaching the target angle.
  bool getLastTimedOut() { return _lastTimedOut; }

  /// @brief Starts a motor-driven calibration spin, if the required sensors and motors are ready.
  bool start() override {
    if (!(_compass.isOk() && _movement.isOk() && _motors.isOk())) {
      _display.print("e2");
      return false;
    }
    Monitor.println();
    Monitor.println("--- STARTING COMPASS CALIBRATION ---");
    Monitor.println();
    _compass.startCalibration();
    _degrees = 0;
    _startedAt = _lastTick = millis();
    _active = true;
    _motors.move("turn_right");
    return true;
  }

  /// @brief Cancels an in-progress spin without computing/applying a result.
  void cancel() override {
    _motors.move("stop");
    _active = false;
    _compass.cancelCalibration();
  }

  /// @brief Feeds the current gyro rate into the calibration spin's rotation integrator and reports when it finishes.
  bool update(int now) override {
    if (!_active) return false;
    float ax=0,ay=0,az=0,rx=0,ry=0,rz=0;
    _movement.get(&ax, &ay, &az, &rx, &ry, &rz);
    if (_step(now, rz)) {
      _reportFinished();
      return true;
    }
    return false;
  }

  /// @brief Prints the compass calibration spin's cumulative rotation while it is in progress.
  void showStatus() override {
    if (isActive()) {
      Monitor.print(" CAL: ");
      monitorPrintLeftJustified(getDegrees(), 6);
      Monitor.print("deg");
    }
  }
};

#endif
