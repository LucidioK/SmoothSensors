#include <Arduino.h>
#include <Arduino_RouterBridge.h>
#include <Arduino_Modulino.h>
#include <Wire.h>
#include "SmoothCompass.h"
#include "SmoothDistance.h"
#include "SmoothMovement.h"
#include "LedMatrixDisplay.h"
#include "RobotMotors.h"

// Sentinel distance used when the distance sensor has no usable reading.
#define INFINITE_DISTANCE 1000000
// Status output interval while the robot is stationary.
#define STATUS_TIMESPAN_WHEN_NOT_MOVING_MS 2000
// Status output interval while the robot is moving.
#define STATUS_TIMESPAN_WHEN_MOVING_MS 200
// Cumulative rotation (degrees) at which a compass calibration spin stops; overshoots 360 to absorb gyro lag/undershoot.
#define CALIBRATION_TARGET_DEGREES 380.0f
// Hard timeout for a calibration spin, guarding against the robot being stuck or lifted mid-spin.
#define CALIBRATION_TIMEOUT_MS     8000
// Gyro Z-axis rate below which rotation is treated as zero-bias noise rather than real motion, during calibration.
#define CALIBRATION_RZ_DEADBAND_DPS 3.0f
// Minimum accumulated rotation (degrees) for a calibration spin to be considered valid; guards against
// accepting a spin that barely moved (e.g. stuck/lifted) just because the raw magnetic span happened to clear
// SmoothCompass's own (much smaller) noise-floor check.
#define CALIBRATION_MIN_VALID_DEGREES 300.0f

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
  /// @brief Instance of the RobotMotors class for managing the robot's motors.
  RobotMotors _robotMotors;
  /// @brief Instance of the SmoothCompass class for reading robot bearings with relation to Earth.
  SmoothCompass _compass;
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
  /// @brief Whether a compass calibration spin is currently in progress.
  bool  _calibrating = false;
  /// @brief Timestamp when the current calibration spin started.
  int   _calibrationStartedAt = 0;
  /// @brief Timestamp of the last calibration rotation-rate integration tick.
  int   _calibrationLastTick = 0;
  /// @brief Cumulative rotation, in degrees, accumulated during the current calibration spin.
  float _calibrationDegrees = 0;

  /// @brief Prints the current distance sensor status to the monitor.
    void _showDistance() {
      if (_distanceOk)
      {
        Monitor.print(" DST: ");
        Monitor.print(_distanceCm);
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
      Monitor.print(" ax="); Monitor.print(ax);
      Monitor.print(" ay="); Monitor.print(ay);
      Monitor.print(" az="); Monitor.print(az);
      Monitor.print(" rx="); Monitor.print(rx);
      Monitor.print(" ry="); Monitor.print(ry);
      Monitor.print(" rz="); Monitor.print(rz);
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
      Monitor.print(_compass.getDirectionAngle());
      Monitor.print(" ");
      Monitor.print(_compass.getDirectionBearing());
    }
  }

  /// @brief Prints the compass calibration spin's cumulative rotation while it is in progress.
  void _showCalibration() {
    if (_calibrating) {
      Monitor.print(" CAL: ");
      Monitor.print(_calibrationDegrees, 1);
      Monitor.print("deg");
    }
  }

  void _monitorDistance(int now) {
    _distanceCm = _distance.getDistanceCm();
    if (now - _previousDistanceRead > STATUS_TIMESPAN_WHEN_MOVING_MS / 10)
    {
      // Refresh the distance reading more often than the status output.
      _distanceCm = _distanceCm ? _distanceCm : INFINITE_DISTANCE;
      _previousDistanceRead = now;
      if (!_calibrating) {
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

  /// @brief Integrates gyro rotation during a calibration spin and ends it once the target angle or timeout is reached.
  void _updateCalibration(int now) {
    if (!_calibrating) return;
    int dt = now - _calibrationLastTick;
    _calibrationLastTick = now;
    float ax=0,ay=0,az=0,rx=0,ry=0,rz=0;
    _movement.get(&ax, &ay, &az, &rx, &ry, &rz);
    float rate = fabs(rz);
    // Below this rate, treat rz as gyro zero-bias noise rather than real rotation, to avoid slowly accumulating phantom degrees while stationary.
    if (rate > CALIBRATION_RZ_DEADBAND_DPS) {
      _calibrationDegrees += rate * dt / 1000.0f;
    }
    if (_calibrationDegrees >= CALIBRATION_TARGET_DEGREES) {
      _finishCalibration(false);
    } else if (now - _calibrationStartedAt >= CALIBRATION_TIMEOUT_MS) {
      _finishCalibration(true);
    }
  }

  /// @brief Starts a motor-driven compass calibration spin, if the required sensors and motors are ready.
  bool _startCalibration() {
    if (!(_compassOk && _movementOk && _motorsOk)) {
      show_text("e2");
      return false;
    }
    _compass.startCalibration();
    _calibrationDegrees = 0;
    _calibrationStartedAt = _calibrationLastTick = millis();
    _alreadyAlertedAboutDistance = false;
    _calibrating = true;
    _statusTimeSpan = STATUS_TIMESPAN_WHEN_MOVING_MS;
    _robotMotors.move("turn_right");
    return true;
  }

  /// @brief Stops the calibration spin, computes and reports the resulting compass correction.
  void _finishCalibration(bool timedOut) {
    _robotMotors.move("stop");
    _calibrating = false;
    _statusTimeSpan = STATUS_TIMESPAN_WHEN_NOT_MOVING_MS;
    bool ok;
    if (_calibrationDegrees >= CALIBRATION_MIN_VALID_DEGREES) {
      ok = _compass.finishCalibration();
    } else {
      _compass.cancelCalibration();
      ok = false;
    }

    Monitor.println();
    Monitor.println("--- COMPASS CALIBRATION ---");
    Monitor.print("spin="); Monitor.print(_calibrationDegrees, 2);
    Monitor.print(" deg  timeout="); Monitor.print(timedOut ? "yes" : "no");
    Monitor.print("  samples_ok="); Monitor.println(ok ? "yes" : "no");
    if (timedOut && _calibrationDegrees < 360.0f) {
      Monitor.println("WARNING: partial arc, constants unreliable");
    }
    if (ok) {
      Monitor.print("offset_x="); Monitor.print(_compass.getOffsetX(), 4);
      Monitor.print(" offset_y="); Monitor.println(_compass.getOffsetY(), 4);
      Monitor.print("scale_x="); Monitor.print(_compass.getScaleX(), 4);
      Monitor.print(" scale_y="); Monitor.print(_compass.getScaleY(), 4);
      Monitor.print("  radius="); Monitor.println(_compass.getCalibrationRadius(), 4);
      Monitor.println("Paste into SmoothCompass.h:");
      Monitor.print("  DEFAULT_OFFSET_X = "); Monitor.print(_compass.getOffsetX(), 4); Monitor.println("f;");
      Monitor.print("  DEFAULT_OFFSET_Y = "); Monitor.print(_compass.getOffsetY(), 4); Monitor.println("f;");
      Monitor.print("  DEFAULT_SCALE_X = "); Monitor.print(_compass.getScaleX(), 4); Monitor.println("f;");
      Monitor.print("  DEFAULT_SCALE_Y = "); Monitor.print(_compass.getScaleY(), 4); Monitor.println("f;");
    } else if (_calibrationDegrees < CALIBRATION_MIN_VALID_DEGREES) {
      Monitor.print("insufficient rotation: "); Monitor.print(_calibrationDegrees, 1);
      Monitor.print(" deg, need >= "); Monitor.println(CALIBRATION_MIN_VALID_DEGREES, 1);
    } else {
      Monitor.print("Calibration error: "); Monitor.println(_compass.getError());
    }
    Monitor.println("---------------------------");
    Monitor.flush();

    show_text(ok ? "cal" : "e3");
  }

  void _showMonitorLine(int now) {
    int timespan = now - _previousTimestamp;
    if (timespan > STATUS_TIMESPAN_WHEN_MOVING_MS)
    {
      Monitor.flush();
  
      if (!_alreadyShowedAppName) {
        // Monitor.println many times does not work in setup()
        Monitor.println();
        Monitor.println("=========================== SmoothSensors003...");
        Monitor.println();
        _alreadyShowedAppName = true;
      }

      _hl = (_hl == HIGH) ? LOW : HIGH;
      digitalWrite(LED_BUILTIN, _hl);
      _previousTimestamp = now;

      _showDistance();

      _showMovement();

      _showMotorStatus();

      _showCompass();

      _showCalibration();

      Monitor.println();
      Monitor.flush();
    }
  }

public:
  /// @brief Creates a sketch controller with the stationary status interval.
  SketchClass() {
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
  
    _updateCalibration(now);

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
    if (_calibrating) {
      if (command == "calibrate_compass") {
        // Already calibrating: ignore the repeat trigger and let the in-progress spin continue.
        return true;
      }
      _robotMotors.move("stop");
      _calibrating = false;
      _compass.cancelCalibration();
      _statusTimeSpan = STATUS_TIMESPAN_WHEN_NOT_MOVING_MS;
    }

    if (command == "calibrate_compass") {
      return _startCalibration();
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
