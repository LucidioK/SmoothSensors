
#ifndef _IMOTORCALIBRATION_H_
#define _IMOTORCALIBRATION_H_
#include <Arduino.h>

class IMotorCalibration
{
public:
  virtual ~IMotorCalibration() {}

  virtual bool start() = 0;
  virtual void cancel() = 0;
  virtual bool update(int now) = 0;
  virtual bool isActive() = 0;
  virtual void showStatus() = 0;
};
#endif
