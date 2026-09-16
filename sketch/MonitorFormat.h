
#ifndef _MONITORFORMAT_H_
#define _MONITORFORMAT_H_
#include <Arduino.h>
#include <Arduino_RouterBridge.h>

template <typename T> inline int _monitorIntJust(T num, int size) {
  int numberOfDigits = (int)num == 0 ? 1 : 0;
  int isNegative = num < 0 ? 1 : 0;
  for (int n = abs((int)num); n > 0;  numberOfDigits++, n /= 10);
  int just = max(0, size - numberOfDigits - isNegative);
  return just;
}

inline void monitorPrintLeftJustified(int num, int size) {
  int just = _monitorIntJust(num, size);
  for (int i = 0; i < just; i++) {
    Monitor.print(" ");
  }
  Monitor.print(num);
}

inline void monitorPrintLeftJustified(float num, int size) {
  int just = _monitorIntJust(num, size - 3);
  for (int i = 0; i < just; i++) {
    Monitor.print(" ");
  }
  Monitor.print(num, 2);
}

inline void monitorPrintLeftJustified(String s, int size) {
  int just = max(size - s.length(), 0);

  for (int i = 0; i < just; i++) {
    Monitor.print(" ");
  }
  Monitor.print(s);
}

#endif
