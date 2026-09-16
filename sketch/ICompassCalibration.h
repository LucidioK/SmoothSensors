
#ifndef _ICOMPASSCALIBRATION_H_
#define _ICOMPASSCALIBRATION_H_
#include <Arduino.h>

class ICompassCalibration
{
public:
  virtual ~ICompassCalibration() {}

  virtual bool start() = 0;
  virtual void cancel() = 0;
  virtual bool update(int now) = 0;
  virtual bool isActive() = 0;
  virtual bool hasSucceededOnce() = 0;
  virtual void showStatus() = 0;
};
#endif
