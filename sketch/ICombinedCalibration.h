#ifndef _ICOMBINEDCALIBRATION_H_
#define _ICOMBINEDCALIBRATION_H_
#include <Arduino.h>

class ICombinedCalibration
{
public:
  virtual ~ICombinedCalibration() {}
  virtual bool start() = 0;
  virtual void cancel() = 0;
  virtual bool update(int now) = 0;
  virtual bool isActive() = 0;
};
#endif
