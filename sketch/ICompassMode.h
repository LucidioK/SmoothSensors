#ifndef _ICOMPASSMODE_H_
#define _ICOMPASSMODE_H_
#include <Arduino.h>

class ICompassMode
{
public:
  virtual ~ICompassMode() {}
  virtual bool start() = 0;
  virtual void cancel() = 0;
  virtual bool isActive() = 0;
  virtual void showBearing() = 0;
  virtual void showStatus() = 0;
};
#endif
