#ifndef _ROBOT_MOTORS_
#define _ROBOT_MOTORS_ 1
#include <Arduino.h>
#include <Arduino_Modulino.h>

class RobotMotors
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

  static uint8_t _clamp(int value) { return (uint8_t)constrain(value, 0, MAX_SPEED_PERCENT); }

  void _drive(bool invertA, bool invertB, int speedA, int speedB) {
    _motors.setInvertA(invertA);
    _motors.setInvertB(invertB);
    _motors.setSpeedA(_clamp(speedA));
    _motors.setSpeedB(_clamp(speedB));
  }

public:
  RobotMotors() {}
  virtual ~RobotMotors()
  {
  }

  bool initialize()
  {
    _ok = _motors.begin();
    if (!_ok)
    {
      return false;
    }
    _motors.setStepperModeEnabled(false);
    return _ok;
  }

  // Motor A drives the left wheel, Motor B the right wheel.
  bool move(String command)
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

  String getStatus()
  {
    return _status;
  }

  static uint8_t getDriveSpeed() { return DRIVE_SPEED; }

  int8_t getStraightBias() { return _straightBias; }
  void setStraightBias(int8_t bias) { _straightBias = (int8_t)constrain((int)bias, -(int)MAX_STRAIGHT_BIAS, (int)MAX_STRAIGHT_BIAS); }
  uint8_t getTurnSpeed() { return _turnSpeed; }
  void setTurnSpeed(uint8_t speed) { _turnSpeed = _clamp(speed); }

  // Drives a turn at an explicit power without mutating _turnSpeed -- used by the calibration ramp so a
  // cancelled/failed calibration run leaves no bogus turn speed behind.
  bool turnAtSpeed(const String& command, uint8_t speed) {
    if (!_ok) return false;
    if (command == "turn_right") { _status = "TRG"; _drive(true, false, speed, speed); return true; }
    if (command == "turn_left")  { _status = "TLF"; _drive(false, true, speed, speed); return true; }
    return false;
  }
};

#endif
