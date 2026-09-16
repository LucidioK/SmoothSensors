
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
  virtual bool    turnAtSpeed(const String& command, uint8_t speed) = 0;
  virtual uint8_t getDriveSpeed() = 0;
  virtual int8_t  getStraightBias() = 0;
  virtual void    setStraightBias(int8_t bias) = 0;
  virtual uint8_t getTurnSpeed() = 0;
  virtual void    setTurnSpeed(uint8_t speed) = 0;
  virtual float   getTurnRateDps() = 0;
  virtual void    setTurnRateDps(float dps) = 0;
  virtual float   getTurnCoastDegrees() = 0;
  virtual void    setTurnCoastDegrees(float degrees) = 0;
  virtual bool    isBearingCommand(const String& command) = 0;
  virtual bool    startPointing(const String& command, bool compassCalibrated) = 0;
  virtual bool    updatePointing(int now) = 0;
  virtual void    cancelPointing() = 0;
  virtual bool    isPointing() = 0;
  virtual bool    isPointingAt(const String& command) = 0;
  virtual void    showPointingStatus() = 0;
};
#endif
