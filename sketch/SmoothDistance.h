
#ifndef _SMOOTHDISTANCE_H_
#define _SMOOTHDISTANCE_H_
#include <Arduino.h>
#include "Modulino.h"

class SmoothDistance
{
private:
  static const int REGISTER_COUNT = 16;
  ModulinoDistance _distance;
  int _position = 0;
  int _distances_cm[REGISTER_COUNT] = { 0 };
  bool _already_warned_data_not_ready = false;

  float _average(float* nums)
  {
    float
      sum = 0,
      min_value = nums[0],
      max_value = nums[0];
    for (int i =0; i < REGISTER_COUNT; i++)
    {
      sum += nums[i];
      min_value = nums[i] < min_value ?  nums[i] : min_value;
      max_value = nums[i] > max_value ?  nums[i] : max_value;
    }
    return (sum - min_value - max_value) / (REGISTER_COUNT-2);
   
  }

public:
  SmoothDistance() {}
  virtual ~SmoothDistance()
  {
  }

  bool initialize()
  {
    Modulino.begin();
    _distance.begin();
    return true;
  }

  void record()
  {
    uint8_t new_data_ready = 0,
            status = 0;
    int now = millis();
    while (!_distance.available() && millis() - now < 100)
    {
      delay(10);
    }

    if (_distance.available())
    {
      _distances_cm[_position++] = _distance.get() / 10;
      _position %= REGISTER_COUNT;
    }
  }

  int getDistanceCm() 
  {
    int
      sum = 0,
      min_value = _distances_cm[0],
      max_value = _distances_cm[0];
    for (int i =0; i < REGISTER_COUNT; i++)
    {
      sum += _distances_cm[i];
      min_value = _distances_cm[i] < min_value ?  _distances_cm[i] : min_value;
      max_value = _distances_cm[i] > max_value ?  _distances_cm[i] : max_value;
    }
    return (sum - min_value - max_value) / (REGISTER_COUNT-2);    
  }
};
#endif