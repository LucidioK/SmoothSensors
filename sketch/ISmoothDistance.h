
#ifndef _ISMOOTHDISTANCE_H_
#define _ISMOOTHDISTANCE_H_
#include <Arduino.h>

class ISmoothDistance
{
public:
  virtual ~ISmoothDistance() {}

  virtual bool initialize() = 0;
  virtual void record() = 0;
  virtual int  getDistanceCm() = 0;
};
#endif
