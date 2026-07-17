#ifndef _SMOOTH_MOVEMENT_
#define _SMOOTH_MOVEMENT_ 1
#include <Arduino_Modulino.h>

class SmoothMovement
{
private:
  static const unsigned int REGISTER_COUNT = 16;
  ModulinoMovement _movement;
  int _position = 0;
  float _acc_x[REGISTER_COUNT] = { 0 };
  float _acc_y[REGISTER_COUNT] = { 0 };
  float _acc_z[REGISTER_COUNT] = { 0 };
  float _rot_x[REGISTER_COUNT] = { 0 };
  float _rot_y[REGISTER_COUNT] = { 0 };
  float _rot_z[REGISTER_COUNT] = { 0 };

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
  SmoothMovement() {}
  virtual ~SmoothMovement()
  {
  }

  bool initialize()
  {
    Modulino.begin();
    return _movement.begin();
  }

  void record()
  {
    _movement.update();

    // Get acceleration values (in g-force, where 1g ≈ 9.8 m/s²)
    _acc_x[_position] = _movement.getX();  // X-axis acceleration
    _acc_y[_position] = _movement.getY();  // Y-axis acceleration
    _acc_z[_position] = _movement.getZ();  // Z-axis acceleration (typically ~1.0 when upright due to gravity)
    
    // Get gyroscope values (in degrees per second)
    _rot_x[_position] = _movement.getRoll();   // Rotation around X-axis (left/right tilt)
    _rot_y[_position] = _movement.getPitch(); // Rotation around Y-axis (forward/backward tilt)
    _rot_z[_position] = _movement.getYaw();     // Rotation around Z-axis (spinning)
    _position++;
    _position %= REGISTER_COUNT;
  }

  
  void get(float* ax, float* ay, float* az, float* rx, float* ry, float* rz)
  {
    *ax = _average(_acc_x);
    *ay = _average(_acc_y);
    *az = _average(_acc_z);
    *rx = _average(_rot_x);
    *ry = _average(_rot_y);
    *rz = _average(_rot_z);
  }  
};

#endif