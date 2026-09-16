
#ifndef _ILEDMATRIXDISPLAY_H_
#define _ILEDMATRIXDISPLAY_H_
#include <Arduino.h>

class ILedMatrixDisplay
{
public:
  virtual ~ILedMatrixDisplay() {}

  virtual bool initialize() = 0;
  virtual void print(const char* text) = 0;
};
#endif
