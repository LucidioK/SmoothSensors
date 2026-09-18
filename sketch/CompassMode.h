#ifndef _COMPASS_MODE_
#define _COMPASS_MODE_ 1
#include <Arduino.h>
#include <Arduino_RouterBridge.h>
#include <Wire.h>
#include "ICompassMode.h"
#include "ISmoothCompass.h"
#include "ILedMatrixDisplay.h"

/// @brief Continuously displays the compass bearing on the LED matrix while active. Display-only -- never
/// drives motors or reads the movement sensor. Cancellation happens externally: SketchClass::showTextImplementation()
/// cancels this feature whenever ANY other text is about to be shown on the LED matrix (see sketch.ino), so this
/// class itself has no cancellation logic beyond a plain cancel() flag flip.
class CompassMode : public ICompassMode
{
private:
  ISmoothCompass& _compass;
  ILedMatrixDisplay& _display;
  bool _active = false;
  String _lastShown = "";

public:
  CompassMode(ISmoothCompass& compass, ILedMatrixDisplay& display)
    : _compass(compass), _display(display) {}

  bool isActive() override { return _active; }

  /// @brief Starts (or, if already active, just re-arms the next redraw for) compass mode. Fails with "e9"
  /// if the compass sensor isn't initialized. Does not require a successful compass calibration this boot --
  /// an uncalibrated bearing is merely skewed, not dangerous, unlike a closed-loop point-to-bearing turn.
  bool start() override {
    if (!_compass.isOk()) {
      _display.print("e9");
      return false;
    }
    _active = true;
    _lastShown = ""; // force a redraw on the next showBearing() call
    return true;
  }

  /// @brief Deactivates compass mode without writing to the display -- the caller (showTextImplementation)
  /// is about to write its own text, which should win.
  void cancel() override {
    _active = false;
    _lastShown = "";
  }

  /// @brief Writes the current bearing directly to the display, bypassing show_text/showTextImplementation
  /// entirely -- that path is compass mode's own cancellation hook, so routing through it would self-cancel
  /// every refresh tick. No-ops if not active, and skips redundant redraws when the bearing hasn't changed.
  void showBearing() override {
    if (!_active) return;
    String bearing = _compass.getDirectionBearing();
    if (bearing == _lastShown) return;
    _lastShown = bearing;
    _display.print(bearing.c_str());
  }

  void showStatus() override {
    if (_active) Monitor.print(" CMD: on");
  }
};
#endif
