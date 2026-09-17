#include <Arduino.h>
#include <Arduino_RouterBridge.h>
#include <Arduino_Modulino.h>
#include <Wire.h>
#include "SmoothCompass.h"
#include "SmoothDistance.h"
#include "SmoothMovement.h"
#include "LedMatrixDisplay.h"
#include "RobotMotors.h"
#include "CompassCalibration.h"
#include "MotorCalibration.h"
#include "CombinedCalibration.h"
#include "MonitorFormat.h"


// Sentinel distance used when the distance sensor has no usable reading.
#define INFINITE_DISTANCE 1000000
// Status output interval while the robot is stationary.
#define STATUS_TIMESPAN_WHEN_NOT_MOVING_MS 2000
// Status output interval while the robot is moving. Also gates _monitorDistance's distance-sensor refresh
// cadence (_statusTimeSpan/10) -- at 200ms this made sensor polling too coarse for MotorCalibration's
// turn-measure phases to see a fresh gyro reading in time for their plateau detection, causing spurious e8
// failures on real hardware; 50ms fixed it (confirmed on-device).
#define STATUS_TIMESPAN_WHEN_MOVING_MS 50

// RPC handler that writes a short message to the LED matrix.
bool show_text(String text);
// RPC handler that sends a movement command to the motors.
bool move(String command);

/// @brief This class encapsulates the main logic of the sketch, managing the distance sensor, movement sensor, LED matrix display, and robot motors. It handles initialization, periodic updates, and communication with external commands through the Arduino Router Bridge.
class SketchClass {
private:
  /// @brief Instance of the SmoothDistance class for managing distance sensor readings.
  SmoothDistance _distance;
  /// @brief Instance of the SmoothMovement class for managing movement sensor readings.
  SmoothMovement _movement;
  /// @brief Instance of the LedMatrixDisplay class for managing the LED matrix display.
  LedMatrixDisplay _ledMatrix;
  /// @brief Instance of the SmoothCompass class for reading robot bearings with relation to Earth.
  SmoothCompass _compass;
  /// @brief Instance of the RobotMotors class for managing the robot's motors.
  RobotMotors _robotMotors;
  /// @brief Drives the motor-powered, gyro-closed-loop compass calibration spin.
  CompassCalibration _calibration;
  /// @brief Drives the motor-powered, gyro-closed-loop motor calibration (straight-line bias + turn power).
  MotorCalibration _motorCalibration;
  /// @brief Runs the motor calibration followed by the compass calibration as one "calibrate" command.
  CombinedCalibration _combinedCalibration;
  /// @brief Time span for status updates, which varies based on whether the robot is moving or not.
  int _statusTimeSpan = STATUS_TIMESPAN_WHEN_NOT_MOVING_MS;

  /// @brief Timestamp of the last status update, used to determine when to perform the next update.
  int _previousTimestamp;
  /// @brief Timestamp of the last distance reading, used to control the frequency of distance updates.
  int _previousDistanceRead;
  
  /// @brief Current logic level used for the built-in LED heartbeat.
  int _hl = HIGH;
  /// @brief Most recently smoothed distance reading in centimeters.
  int _distanceCm = INFINITE_DISTANCE;
  /// @brief Distance threshold that triggers an emergency stop.
  int _minimumDistance = 10;
  /// @brief Whether the distance sensor initialized successfully.
  bool _distanceOk = false;
  /// @brief Whether the compass sensor initialized successfully.
  bool _compassOk = false;
  /// @brief Whether the movement sensor initialized successfully.
  bool _movementOk = false;
  /// @brief Whether the motors initialized successfully.
  bool _motorsOk = false;
  /// @brief Whether the obstacle warning is currently active.
  bool _alreadyAlertedAboutDistance = false;
  /// @brief Whether the startup banner has already been printed.
  bool _alreadyShowedAppName = false;

  /// @brief Prints the current distance sensor status to the monitor.
    void _showDistance() {
      if (_distanceOk)
      {
        Monitor.print(" DST: ");
        monitorPrintLeftJustified(_distanceCm, 3);
        Monitor.print("cm");
      }
      else
      {
          Monitor.print("Distance NOK");
      }  
  }
  
  /// @brief Prints the current smoothed movement sensor status to the monitor.
  void _showMovement() {
    if (_movementOk) {
      // Smoothed acceleration and rotation values for each sensor axis.
      float ax=0,ay=0,az=0,rx=0,ry=0,rz=0;
      _movement.get(&ax, &ay, &az, &rx, &ry, &rz);
      Monitor.print(" ax="); monitorPrintLeftJustified(ax, 6);
      Monitor.print(" ay="); monitorPrintLeftJustified(ay, 6);
      Monitor.print(" az="); monitorPrintLeftJustified(az, 6);
      Monitor.print(" rx="); monitorPrintLeftJustified(rx, 6);
      Monitor.print(" ry="); monitorPrintLeftJustified(ry, 6);
      Monitor.print(" rz="); monitorPrintLeftJustified(rz, 6);
    }
    else
    {
      Monitor.print(" Movement NOK");
    }  
  }
  
  /// @brief Prints the current motor command status to the monitor.
  void _showMotorStatus() {
    Monitor.print(" MOT: ");
    Monitor.print(_robotMotors.getStatus());
  }

  void _showCompass() {
    Monitor.print(" CMP: ");
    Monitor.print(_compass.getError());
    if (_compassOk) {
      Monitor.print(" ");
      monitorPrintLeftJustified(_compass.getDirectionAngle(), 6);
      Monitor.print(" ");
      monitorPrintLeftJustified(_compass.getDirectionBearing(), 3);
    }
  }

  void _monitorDistance(int now) {
    _distanceCm = _distance.getDistanceCm();
    if (now - _previousDistanceRead > _statusTimeSpan / 10)
    {
      // Refresh the distance reading more often than the status output.
      _distanceCm = _distanceCm ? _distanceCm : INFINITE_DISTANCE;
      _previousDistanceRead = now;
      if (!_calibration.isActive() && !_robotMotors.isPointing()) {
        if (_distanceCm < 10) {
          move("stop");
          if (_motorsOk) {
            // Convert the short distance value before displaying it on the matrix.
            char buf[4];
            show_text(String(itoa(_distanceCm, buf, 10)));
          }
          _alreadyAlertedAboutDistance = true;
        }
        else if (_alreadyAlertedAboutDistance) {
          _alreadyAlertedAboutDistance = false;
          if (_motorsOk) {
            show_text("_");
          }
        }
      }
    }
  }

  void _onFeatureStarted() {
    _alreadyAlertedAboutDistance = false;
    _statusTimeSpan = STATUS_TIMESPAN_WHEN_MOVING_MS;
  }

  void _onFeatureStopped() {
    _statusTimeSpan = STATUS_TIMESPAN_WHEN_NOT_MOVING_MS;
  }

  void _showMonitorLine(int now) {
    int timespan = now - _previousTimestamp;
    if (timespan > _statusTimeSpan)
    {
      Monitor.flush();
  
      if (!_alreadyShowedAppName) {
        // Monitor.println many times does not work in setup()
        Monitor.println();
        Monitor.println("=========================== SmoothSensors003...");
        Monitor.println();
        Monitor.flush();
        _alreadyShowedAppName = true;
      }

      monitorPrintLeftJustified(now, 8);
      _hl = (_hl == HIGH) ? LOW : HIGH;
      digitalWrite(LED_BUILTIN, _hl);
      _previousTimestamp = now;

      _showDistance();

      _showMovement();

      _showMotorStatus();

      _showCompass();

      _calibration.showStatus();

      _motorCalibration.showStatus();

      _robotMotors.showPointingStatus();

      Monitor.println();
      Monitor.flush();
    }
  }

public:
  /// @brief Creates a sketch controller with the stationary status interval.
  SketchClass() : _robotMotors(_compass, _movement, _ledMatrix), _calibration(_compass, _movement, _robotMotors, _ledMatrix), _motorCalibration(_robotMotors, _movement, _ledMatrix), _combinedCalibration(_motorCalibration, _calibration) {
    _statusTimeSpan = STATUS_TIMESPAN_WHEN_NOT_MOVING_MS;
  }

  /// @brief Releases the sketch controller resources.
  virtual ~SketchClass() {}

  /// @brief Initializes hardware, the monitor, and RPC handlers.
  void setup() {
    // Initialize the I2C bus, monitor, and built-in LED.
    Wire.begin();
    delay(500);
    Monitor.begin(9600);
    delay(2000);
    Monitor.flush();
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, _hl);

    // Initialize the sketch controller state and RPC handlers.
    _previousTimestamp = millis();
    Bridge.begin();
    Bridge.provide("show_text", show_text);
    Bridge.provide("move", move);

    // Initialize the distance sensor, movement sensor, LED matrix display, and robot motors.
    _distanceOk = _distance.initialize();
    _movementOk = _movement.initialize();
    _compassOk  = _compass.initialize();
    _ledMatrix.initialize();
    _motorsOk = _robotMotors.initialize();

    // Display the ready status on the LED matrix.
    if (_motorsOk) {
      show_text("rdy");
    } else {
      show_text("e1");
    }
  }

  /// @brief Records sensors and periodically reports status or stops at obstacles.
  void loop() {
    // Current loop timestamp and elapsed time since the last status report.
    int now = millis();

    if (_compassOk) {
      _compass.record();
    }
    
    if (_distanceOk) {
      _distance.record();
    }
  
    if (_movementOk) {
      _movement.record();
    }
  
    if (_combinedCalibration.update(now)) {
      _onFeatureStopped();
    }
    if (_robotMotors.updatePointing(now)) {
      _onFeatureStopped();
    }

    _monitorDistance(now);
    _showMonitorLine(now);
  
  }

  /// @brief Writes an RPC text command to the LED matrix.
  bool showTextImplementation(String text)
  {
    _ledMatrix.print(text.c_str());
    return true;
  }

  /// @brief Applies an RPC movement command and updates the report interval.
  bool moveImplementation(String command)
  {
    bool alreadyStopped = false;

    if (_combinedCalibration.isActive()) {
      if (command == "calibrate") {
        // Already calibrating: ignore the repeat trigger and let the in-progress sequence continue.
        return true;
      }
      _combinedCalibration.cancel();
      _onFeatureStopped();
      alreadyStopped = true;
    }

    if (_robotMotors.isPointing()) {
      if (_robotMotors.isPointingAt(command)) return true;
      _robotMotors.cancelPointing();
      _onFeatureStopped();
      alreadyStopped = true;
    }

    if (command == "calibrate") {
      bool ok = _combinedCalibration.start();
      if (ok) _onFeatureStarted();
      return ok;
    }

    if (_robotMotors.isBearingCommand(command)) {
      bool ok = _robotMotors.startPointing(command, _calibration.hasSucceededOnce());
      if (ok) _onFeatureStarted();
      return ok;
    }

    if (alreadyStopped && command == "stop") {
      // A cancellation above already stopped the motors (which blocks ~400ms) and called
      // _onFeatureStopped(); avoid stopping twice.
      return true;
    }

    _statusTimeSpan = command == "stop" ? STATUS_TIMESPAN_WHEN_NOT_MOVING_MS : STATUS_TIMESPAN_WHEN_MOVING_MS;
    return _robotMotors.move(command);
  }

  /// @brief Whether the motors initialized successfully.
  bool isMotorOk() { return _motorsOk; }
};

// Global controller used by the Arduino framework callbacks and RPC handlers.
SketchClass sketch;

// Forwards the global text RPC to the sketch controller.
bool show_text(String text)
{
  return sketch.showTextImplementation(text);
}

// Forwards the global movement RPC to the sketch controller.
bool move(String command)
{
  if (!sketch.isMotorOk())
  {
    show_text("e1");
    return false;
  }
  return sketch.moveImplementation(command);
}

// Arduino framework entry point for one-time sketch initialization.
void setup() {
  sketch.setup();
}

// Arduino framework entry point called repeatedly while the sketch runs.
void loop() {
  sketch.loop();
}
