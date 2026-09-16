
#ifndef _ISMOOTHMOVEMENT_H_
#define _ISMOOTHMOVEMENT_H_
#include <Arduino.h>

class ISmoothMovement
{
public:
  virtual ~ISmoothMovement() {}

  virtual bool initialize() = 0;
  virtual void record() = 0;
  virtual bool isOk() = 0;
  virtual void get(float* ax, float* ay, float* az, float* rx, float* ry, float* rz) = 0;
};
#endif
