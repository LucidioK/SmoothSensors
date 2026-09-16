
#ifndef _ISMOOTHCOMPASS_H_
#define _ISMOOTHCOMPASS_H_
#include <Arduino.h>

class ISmoothCompass
{
public:
  virtual ~ISmoothCompass() {}

  virtual bool   initialize() = 0;
  virtual void   record() = 0;
  virtual bool   isOk() = 0;
  virtual String getError() = 0;
  virtual float  getDirectionAngle() = 0;
  virtual String getDirectionBearing() = 0;
  virtual unsigned int getSampleCount() = 0;
  virtual void   startCalibration() = 0;
  virtual void   cancelCalibration() = 0;
  virtual bool   finishCalibration() = 0;
  virtual float  getOffsetX() = 0;
  virtual float  getOffsetY() = 0;
  virtual float  getScaleX() = 0;
  virtual float  getScaleY() = 0;
  virtual float  getCalibrationRadius() = 0;
};
#endif
