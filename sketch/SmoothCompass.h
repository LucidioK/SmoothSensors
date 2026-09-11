#ifndef _SMOOTH_COMPASS_
#define _SMOOTH_COMPASS_ 1
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_LIS3MDL.h>

class SmoothCompass
{
private:
  static const unsigned int REGISTER_COUNT = 32;
  static const uint8_t I2C_ADDRESS_MIN = 0x1C;
  static const uint8_t I2C_ADDRESS_MAX = 0x1E;
  static const int SDA_PIN = 20;
  static const int SCL_PIN = 21;
  static const int RECOVERY_PULSES = 9;
  static const int INIT_TIMEOUT_MS = 500;
  Adafruit_LIS3MDL _lis;
  int _position = 0;
  float _mag_x[REGISTER_COUNT] = { 0 };
  float _mag_y[REGISTER_COUNT] = { 0 };
  uint8_t _address = 0;
  bool _ok = false;
  String _error = "";

  float _average(float* nums)
  {
    float
      sum = 0,
      min_value = nums[0],
      max_value = nums[0];
    for (int i = 0; i < REGISTER_COUNT; i++)
    {
      sum += nums[i];
      min_value = nums[i] < min_value ? nums[i] : min_value;
      max_value = nums[i] > max_value ? nums[i] : max_value;
    }
    return (sum - min_value - max_value) / (REGISTER_COUNT - 2);
  }

  static bool _probe(uint8_t address)
  {
    Wire1.beginTransmission(address);
    return Wire1.endTransmission() == 0;
  }

public:
  SmoothCompass() {}
  virtual ~SmoothCompass()
  {
  }

  static bool recoverBus()
  {
    pinMode(SDA_PIN, INPUT_PULLUP);
    pinMode(SCL_PIN, INPUT_PULLUP);
    delayMicroseconds(5);

    if (digitalRead(SDA_PIN) == HIGH)
    {
      Wire1.begin();
      return true;
    }

    for (int i = 0; i < RECOVERY_PULSES && digitalRead(SDA_PIN) == LOW; i++)
    {
      pinMode(SCL_PIN, OUTPUT);
      delayMicroseconds(5);
      pinMode(SCL_PIN, INPUT_PULLUP);
      delayMicroseconds(5);
    }

    // Emit a STOP condition so any half-transacted slave resets its state machine.
    pinMode(SDA_PIN, OUTPUT);
    delayMicroseconds(5);
    pinMode(SCL_PIN, INPUT_PULLUP);
    delayMicroseconds(5);
    pinMode(SDA_PIN, INPUT_PULLUP);
    delayMicroseconds(5);

    bool freed = digitalRead(SDA_PIN) == HIGH && digitalRead(SCL_PIN) == HIGH;
    Wire1.begin();
    delay(5);
    return freed;
  }

  bool initialize()
  {
    _ok = false;
    _error = "";

    if (!recoverBus())
    {
      _error = "I2C bus stuck";
      return false;
    }


    for (int address = I2C_ADDRESS_MIN; address <= I2C_ADDRESS_MAX; address++)
    {
      if (_probe(address))
      {
        _address = address;
        break;
      }
      delay(5);
    }

    if (_address == 0)
    {
      _error = "No LIS3MDL";
      return false;
    }

    if (!_lis.begin_I2C(_address, &Wire1))
    {
      _error = "LIS3MDL begin failed";
      return false;
    }

    _ok = true;
    return true;
  }

  String getError()
  {
    return _error;
  }

  void record()
  {
    if (!_ok) return;

    _lis.read();
    _mag_x[_position] = _lis.x;
    _mag_y[_position] = _lis.y;
    _position++;
    _position %= REGISTER_COUNT;
  }

  float getDirectionAngle()
  {
    float degrees = atan2(_average(_mag_y), _average(_mag_x)) * 180.0 / PI;
    if (degrees < 0) degrees += 360.0;
    return degrees;
  }

  String getDirectionBearing()
  {
    static const char* const BEARINGS[16] = {
      "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
      "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
    };
    int index = (int)((getDirectionAngle() + 11.25) / 22.5) % 16;
    return String(BEARINGS[index]);
  }
};

#endif
