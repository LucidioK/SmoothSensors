#ifndef _COMBINED_CALIBRATION_
#define _COMBINED_CALIBRATION_ 1
#include <Arduino.h>
#include "ICombinedCalibration.h"
#include "IMotorCalibration.h"
#include "ICompassCalibration.h"

/// @brief Sequences motor calibration followed by compass calibration as a single "calibrate" command.
/// Pure orchestration -- both sub-features already own their full lifecycle (state, sensor reads, and
/// Monitor/LED reporting), so this class has no dependencies beyond the two interfaces it sequences.
class CombinedCalibration : public ICombinedCalibration
{
private:
  enum Phase { IDLE, MOTORS, COMPASS };
  IMotorCalibration& _motorCalibration;
  ICompassCalibration& _compassCalibration;
  Phase _phase = IDLE;

public:
  CombinedCalibration(IMotorCalibration& motorCalibration, ICompassCalibration& compassCalibration)
    : _motorCalibration(motorCalibration), _compassCalibration(compassCalibration) {}

  bool isActive() override { return _phase != IDLE; }

  /// @brief Starts the sequence at the motor-calibration phase. If motor calibration can't even start
  /// (unmet prerequisites -- it will have already shown its own error code), the whole command aborts.
  bool start() override {
    if (!_motorCalibration.start()) {
      _phase = IDLE;
      return false;
    }
    _phase = MOTORS;
    return true;
  }

  /// @brief Cancels only whichever sub-feature is currently running, then goes idle.
  void cancel() override {
    if (_phase == MOTORS) _motorCalibration.cancel();
    else if (_phase == COMPASS) _compassCalibration.cancel();
    _phase = IDLE;
  }

  /// @brief Advances whichever phase is active. Returns true only on the iteration the WHOLE sequence
  /// ends (either phase's finish, or the compass phase failing to start).
  bool update(int now) override {
    if (_phase == IDLE) return false;

    if (_phase == MOTORS) {
      if (!_motorCalibration.update(now)) return false; // motor phase still running
      // Motor phase just finished -- success or failure both proceed to the compass phase, per the
      // issue's "executes Calibrate Motors then Calibrate Compass in this sequence".
      if (!_compassCalibration.start()) {
        _phase = IDLE;
        return true; // compass phase's own prerequisite gate failed (shows its own error code); sequence over
      }
      _phase = COMPASS;
      // CRITICAL: do not call _compassCalibration.update(now) in this same iteration. CompassCalibration::start()
      // just stamped its own internal timing using a fresh millis() taken after MotorCalibration's blocking
      // stop/brake sequence and Monitor report -- calling update() with this iteration's stale `now` parameter
      // (sampled at the top of loop(), before all that blocking work) would compute a negative dt and corrupt
      // its degree integration. Let the NEXT loop() iteration drive the compass phase for the first time.
      return false;
    }

    // _phase == COMPASS
    if (_compassCalibration.update(now)) {
      _phase = IDLE;
      return true;
    }
    return false;
  }
};
#endif
