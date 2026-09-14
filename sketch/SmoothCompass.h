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
  static constexpr float DEFAULT_OFFSET_X = 0.0f;
  static constexpr float DEFAULT_OFFSET_Y = 0.0f;
  static constexpr float DEFAULT_SCALE_X  = 1.0f;
  static constexpr float DEFAULT_SCALE_Y  = 1.0f;
  // Minimum per-axis span (raw units) for a calibration to be considered valid.
  static constexpr float MIN_CALIBRATION_SPAN = 1.0f;
  Adafruit_LIS3MDL _lis;
  int _position = 0;
  float _mag_x[REGISTER_COUNT] = { 0 };
  float _mag_y[REGISTER_COUNT] = { 0 };
  uint8_t _address = 0;
  bool _ok = false;
  String _error = "";
  float _offset_x = DEFAULT_OFFSET_X;
  float _offset_y = DEFAULT_OFFSET_Y;
  float _scale_x  = DEFAULT_SCALE_X;
  float _scale_y  = DEFAULT_SCALE_Y;
  bool  _calibrating = false;
  bool  _cal_has_sample = false;
  float _cal_min_x, _cal_max_x, _cal_min_y, _cal_max_y;
  float _cal_radius = 0;

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

  void _trackCalibrationSample(float x, float y)
  {
    if (!_cal_has_sample)
    {
      _cal_min_x = _cal_max_x = x;
      _cal_min_y = _cal_max_y = y;
      _cal_has_sample = true;
      return;
    }
    _cal_min_x = x < _cal_min_x ? x : _cal_min_x;
    _cal_max_x = x > _cal_max_x ? x : _cal_max_x;
    _cal_min_y = y < _cal_min_y ? y : _cal_min_y;
    _cal_max_y = y > _cal_max_y ? y : _cal_max_y;
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
    if (_calibrating) _trackCalibrationSample(_lis.x, _lis.y);
    _mag_x[_position] = _lis.x;
    _mag_y[_position] = _lis.y;
    _position++;
    _position %= REGISTER_COUNT;
  }

  float getDirectionAngle()
  {
    float mx = (_average(_mag_x) - _offset_x) * _scale_x;
    float my = (_average(_mag_y) - _offset_y) * _scale_y;
    float degrees = atan2(my, mx) * 180.0 / PI;
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

  void startCalibration()
  {
    _calibrating = true;
    _cal_has_sample = false;
  }

  void cancelCalibration()
  {
    _calibrating = false;
  }

  bool finishCalibration()
  {
    _calibrating = false;
    if (!_cal_has_sample ||
        (_cal_max_x - _cal_min_x) < MIN_CALIBRATION_SPAN ||
        (_cal_max_y - _cal_min_y) < MIN_CALIBRATION_SPAN)
    {
      _error = "cal span too small";
      return false;
    }
    float ox = (_cal_max_x + _cal_min_x) / 2.0f;
    float oy = (_cal_max_y + _cal_min_y) / 2.0f;
    float rx = (_cal_max_x - _cal_min_x) / 2.0f;
    float ry = (_cal_max_y - _cal_min_y) / 2.0f;
    _cal_radius = (rx + ry) / 2.0f;
    _offset_x = ox;
    _offset_y = oy;
    _scale_x = _cal_radius / rx;
    _scale_y = _cal_radius / ry;
    _error = "";
    return true;
  }

  bool isCalibrating() { return _calibrating; }
  float getOffsetX() { return _offset_x; }
  float getOffsetY() { return _offset_y; }
  float getScaleX() { return _scale_x; }
  float getScaleY() { return _scale_y; }
  float getCalibrationRadius() { return _cal_radius; }
  static unsigned int getSampleCount() { return REGISTER_COUNT; }
};

#endif
