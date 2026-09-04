#ifndef _ROBOT_MOTORS_
#define _ROBOT_MOTORS_ 1
#include <Arduino.h>
#include <Arduino_Modulino.h>

class RobotMotors
{
private:
  static const uint8_t DRIVE_SPEED = 90;
  ModulinoMotors _motors;
  bool _ok = false;
  String _status = "";

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
      _motors.setInvertA(true);
      _motors.setInvertB(true);
      _motors.setSpeedA(DRIVE_SPEED);
      _motors.setSpeedB(DRIVE_SPEED);
    }
    else if (command == "turn_right")
    {
      _status = "TRG";
      _motors.setInvertA(true);
      _motors.setInvertB(false);
      _motors.setSpeedA(DRIVE_SPEED);
      _motors.setSpeedB(DRIVE_SPEED);
    }
    else if (command == "turn_left")
    {
      _status = "TLF";
      _motors.setInvertA(false);
      _motors.setInvertB(true);
      _motors.setSpeedA(DRIVE_SPEED);
      _motors.setSpeedB(DRIVE_SPEED);
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
};

#endif
