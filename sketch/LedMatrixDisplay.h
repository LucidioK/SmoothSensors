
#ifndef _LEDMATRIXDISPLAY_H_
#define _LEDMATRIXDISPLAY_H_
#include <Arduino.h>
#include <Arduino_LED_Matrix.h>
#include <ArduinoGraphics.h>

class LedMatrixDisplay
{
private:
  static const int CHAR_COUNT = 3;
  ArduinoLEDMatrix _matrix;

public:
  LedMatrixDisplay() {}
  virtual ~LedMatrixDisplay()
  {
  }

  bool initialize()
  {
    _matrix.begin();
    return true;
  }

  void print(const char* text)
  {
    char padded[CHAR_COUNT + 1];
    int i = 0;
    for (; i < CHAR_COUNT && text[i] != '\0'; i++)
    {
      padded[i] = text[i];
    }
    for (; i < CHAR_COUNT; i++)
    {
      padded[i] = ' ';
    }
    padded[CHAR_COUNT] = '\0';

    _matrix.beginDraw();
    _matrix.stroke(0xFFFFFFFF);
    _matrix.textFont(Font_4x6);
    _matrix.beginText(0, 1, 0xFFFFFF);
    _matrix.println(padded);
    _matrix.endText(NO_SCROLL);
    _matrix.endDraw();
  }
};
#endif
