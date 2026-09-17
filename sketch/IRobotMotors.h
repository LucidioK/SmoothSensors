
#ifndef _IROBOTMOTORS_H_
#define _IROBOTMOTORS_H_
#include <Arduino.h>

class IRobotMotors
{
public:
  virtual ~IRobotMotors() {}

  virtual bool    initialize() = 0;
  virtual bool    isOk() = 0;
  virtual bool    move(String command) = 0;
  virtual String  getStatus() = 0;
  virtual uint8_t getDriveSpeed() = 0;
  virtual float   getIdleRz1() = 0;
  virtual void    setIdleRz1(float rz) = 0;
  virtual float   getMinimumRzWhenTurningRight() = 0;
  virtual void    setMinimumRzWhenTurningRight(float rz) = 0;
  virtual int     getTimeInMillisecondsToReachMinimumRzWhenTurningRight() = 0;
  virtual void    setTimeInMillisecondsToReachMinimumRzWhenTurningRight(int ms) = 0;
  virtual int     getTimeInMillisecondsToStopWhenTurningRight() = 0;
  virtual void    setTimeInMillisecondsToStopWhenTurningRight(int ms) = 0;
  virtual float   getMaximumRzWhenTurningLeft() = 0;
  virtual void    setMaximumRzWhenTurningLeft(float rz) = 0;
  virtual int     getTimeInMillisecondsToReachMaximumRzWhenTurningLeft() = 0;
  virtual void    setTimeInMillisecondsToReachMaximumRzWhenTurningLeft(int ms) = 0;
  virtual int     getTimeInMillisecondsToStopWhenTurningLeft() = 0;
  virtual void    setTimeInMillisecondsToStopWhenTurningLeft(int ms) = 0;
  virtual bool    isBearingCommand(const String& command) = 0;
  virtual bool    startPointing(const String& command, bool compassCalibrated) = 0;
  virtual bool    updatePointing(int now) = 0;
  virtual void    cancelPointing() = 0;
  virtual bool    isPointing() = 0;
  virtual bool    isPointingAt(const String& command) = 0;
  virtual void    showPointingStatus() = 0;
  virtual void    updateGoingStraight(int now) = 0;
  virtual void    showStraightStatus() = 0;
};
#endif
