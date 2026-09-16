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
  static const uint8_t MAX_SPEED_PERCENT   = 100;                             // ModulinoMotors::setSpeedA/B silently reject >100
  static const uint8_t MAX_STRAIGHT_BIAS   = MAX_SPEED_PERCENT - DRIVE_SPEED; // headroom above DRIVE_SPEED (==10 today)
  ModulinoMotors _motors;
  bool _ok = false;
  String _status = "";
  int8_t  _straightBias = 0;   // >0 => robot veers RIGHT => left(A) gets +bias, right(B) gets -bias
  uint8_t _turnSpeed    = DRIVE_SPEED; // pre-calibration fallback == today's exact behavior
  float _turnRateDps = 0;        // degrees/sec measured at _turnSpeed once rotation is fully established
  float _turnCoastDegrees = 0;   // degrees the robot continues to rotate, from momentum, after move("stop") is issued at _turnSpeed

  static const uint8_t BRAKE_POWER_PERCENT = 50; // % of the just-commanded speed used for the reverse-power braking pulse
  static const int BRAKE_PULSE_MS = 120;         // duration of the reverse-power pulse -- long enough to arrest momentum, short enough not to reverse travel

  bool _lastInvertA = false;
  bool _lastInvertB = false;
  uint8_t _lastSpeedA = 0;
  uint8_t _lastSpeedB = 0;

  // Heading tolerance for "close enough" to a point-to-bearing target, absorbing residual smoothing lag and post-calibration magnetic noise.
  static constexpr float POINT_TOLERANCE_DEGREES = 8.0f;
  // Hard timeout for a point-to-bearing turn, bounding every turn/settle/re-check cycle. Turns now run
  // at the calibrated, potentially slower, turn power.
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

  /// @brief Computes how long to actively drive a turn of the given magnitude so that, after the robot's
  /// measured momentum-coast is accounted for, it lands close to the target. Returns -1 if turn rate hasn't
  /// been calibrated yet (RobotMotors::getTurnRateDps() == 0) -- callers must fall back to the compass-threshold
  /// stopping trigger in that case.
  int _computeTurnDurationMs(float errorDegrees) {
    float rateDps = _turnRateDps;
    if (rateDps <= 0.1f) return -1;
    float activeDegrees = errorDegrees - _turnCoastDegrees;
    if (activeDegrees < 0) activeDegrees = 0;
    return (int)(activeDegrees / rateDps * 1000.0f);
  }

  /// @brief Starts (or restarts, on a correction pass) driving a turn toward the given heading error, arming
  /// a precomputed timed cutoff (see _computeTurnDurationMs) alongside the existing compass-threshold/sign-flip
  /// triggers checked in updatePointing.
  void _beginTurn(float error) {
    move(_turnCommandFor(error));
    _pointTurnStartedAt = millis();
    _pointTurnDurationMs = _computeTurnDurationMs(fabsf(error));
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
      _drive(true, true, DRIVE_SPEED + _straightBias, DRIVE_SPEED - _straightBias);
    }
    else if (command == "go_back")
    {
      _status = "GBK";
      _drive(false, false, DRIVE_SPEED + _straightBias, DRIVE_SPEED - _straightBias);
    }
    else if (command == "turn_right")
    {
      _status = "TRG";
      _drive(true, false, _turnSpeed, _turnSpeed);
    }
    else if (command == "turn_left")
    {
      _status = "TLF";
      _drive(false, true, _turnSpeed, _turnSpeed);
    }
    else if (command == "stop")
    {
      _status = "STP";
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

  int8_t getStraightBias() override { return _straightBias; }
  void setStraightBias(int8_t bias) override { _straightBias = (int8_t)constrain((int)bias, -(int)MAX_STRAIGHT_BIAS, (int)MAX_STRAIGHT_BIAS); }
  uint8_t getTurnSpeed() override { return _turnSpeed; }
  void setTurnSpeed(uint8_t speed) override { _turnSpeed = _clamp(speed); }
  float getTurnRateDps() override { return _turnRateDps; }
  void setTurnRateDps(float dps) override { _turnRateDps = dps < 0 ? 0 : dps; }
  float getTurnCoastDegrees() override { return _turnCoastDegrees; }
  void setTurnCoastDegrees(float degrees) override { _turnCoastDegrees = degrees < 0 ? 0 : degrees; }

  // Drives a turn at an explicit power without mutating _turnSpeed -- used by the calibration ramp so a
  // cancelled/failed calibration run leaves no bogus turn speed behind.
  bool turnAtSpeed(const String& command, uint8_t speed) override {
    if (!_ok) return false;
    if (command == "turn_right") { _status = "TRG"; _drive(true, false, speed, speed); return true; }
    if (command == "turn_left")  { _status = "TLF"; _drive(false, true, speed, speed); return true; }
    return false;
  }

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
      _pointState = POINT_TURNING;
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
          _pointState = POINT_TURNING;
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
};

#endif
