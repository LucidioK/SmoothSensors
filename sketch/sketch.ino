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


// Sentinel distance used when the distance sensor has no usable reading.
#define INFINITE_DISTANCE 1000000
// Status output interval while the robot is stationary.
#define STATUS_TIMESPAN_WHEN_NOT_MOVING_MS 2000
// Status output interval while the robot is moving.
#define STATUS_TIMESPAN_WHEN_MOVING_MS 200
// Heading tolerance for "close enough" to a point-to-bearing target, absorbing residual smoothing lag and post-calibration magnetic noise.
#define POINT_TOLERANCE_DEGREES 8.0f
// Hard timeout for a point-to-bearing turn, bounding every turn/settle/re-check cycle.
#define POINT_TIMEOUT_MS 8000
// Minimum dwell time after stopping before trusting a heading reading, giving the ring buffer time to refill with post-stop samples.
#define POINT_SETTLE_MS 500

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
  /// @brief Drives the motor-powered, gyro-closed-loop compass calibration spin.
  CompassCalibration _calibration;
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
  /// @brief States for a point-to-bearing turn: idle, actively turning, or settling before re-checking heading.
  enum PointState { POINT_IDLE, POINT_TURNING, POINT_SETTLING };
  /// @brief Current state of any in-progress point-to-bearing turn.
  PointState _pointState = POINT_IDLE;
  /// @brief RPC command string for the current point-to-bearing turn (e.g. "point_north"), kept for Monitor output and re-trigger detection.
  String _pointCommand = "";
  /// @brief Target heading, in degrees, for the current point-to-bearing turn.
  float _pointTargetDegrees = 0;
  /// @brief Timestamp when the current point-to-bearing operation started; bounds the whole operation via POINT_TIMEOUT_MS.
  int _pointStartedAt = 0;
  /// @brief Timestamp when the current settle period began.
  int _pointSettleStartedAt = 0;
  /// @brief Loop iterations elapsed during the current settle period; waited until >= SmoothCompass::getSampleCount() before trusting the ring buffer.
  unsigned int _pointSettleSamples = 0;
  /// @brief Signed heading error (degrees) from the previous iteration, used to detect a sign flip (overshoot) between loop iterations.
  float _pointLastError = 0;
  ///

  template <typename T> int _intJust(T num, int size) {
    int numberOfDigits = (int)num == 0 ? 1 : 0;
    int isNegative = num < 0 ? 1 : 0;
    for (int n = abs((int)num); n > 0;  numberOfDigits++, n /= 10);
    int just = max(0, size - numberOfDigits - isNegative);    
    return just;
  }

  void _printLeftJustified(int num, int size) {
    int just = _intJust(num, size);
    for (int i = 0; i < just; i++) {
      Monitor.print(" ");
    }
    Monitor.print(num);
  }

  void _printLeftJustified(float num, int size) {
    int just = _intJust(num, size - 3);
    for (int i = 0; i < just; i++) {
      Monitor.print(" ");
    }
    Monitor.print(num, 2);
  }

  void _printLeftJustified(String s, int size) {
    int just = max(size - s.length(), 0);
    
    for (int i = 0; i < just; i++) {
      Monitor.print(" ");
    }
    Monitor.print(s);
  }

  /// @brief Looks up the target heading (degrees) for a "point_*" RPC command string. Returns false if the command isn't a point-to-bearing command.
  bool _bearingTargetDegrees(const String& command, float* degrees) {
    struct BearingEntry { const char* command; float degrees; };
    static const BearingEntry BEARINGS[] = {
      { "point_north",     0.0f },
      { "point_northeast", 45.0f },
      { "point_east",      90.0f },
      { "point_southeast", 135.0f },
      { "point_south",     180.0f },
      { "point_southwest", 225.0f },
      { "point_west",      270.0f },
      { "point_northwest", 315.0f },
    };
    for (unsigned int i = 0; i < sizeof(BEARINGS) / sizeof(BEARINGS[0]); i++) {
      if (command == BEARINGS[i].command) {
        *degrees = BEARINGS[i].degrees;
        return true;
      }
    }
    return false;
  }

  /// @brief Prints the current distance sensor status to the monitor.
    void _showDistance() {
      if (_distanceOk)
      {
        Monitor.print(" DST: ");
        _printLeftJustified(_distanceCm, 3);
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
      Monitor.print(" ax="); _printLeftJustified(ax, 6);
      Monitor.print(" ay="); _printLeftJustified(ay, 6);
      Monitor.print(" az="); _printLeftJustified(az, 6);
      Monitor.print(" rx="); _printLeftJustified(rx, 6);
      Monitor.print(" ry="); _printLeftJustified(ry, 6);
      Monitor.print(" rz="); _printLeftJustified(rz, 6);
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
      _printLeftJustified(_compass.getDirectionAngle(), 6);
      Monitor.print(" ");
      _printLeftJustified(_compass.getDirectionBearing(), 3);
    }
  }

  /// @brief Prints the compass calibration spin's cumulative rotation while it is in progress.
  void _showCalibration() {
    if (_calibration.isActive()) {
      Monitor.print(" CAL: ");
      _printLeftJustified(_calibration.getDegrees(), 6);
      Monitor.print("deg");
    }
  }

  /// @brief Prints the point-to-bearing turn's target/current heading while one is in progress.
  void _showPointing() {
    if (_pointState != POINT_IDLE) {
      Monitor.print(" PNT: ");
      Monitor.print(_pointCommand);
      Monitor.print(" target=");_printLeftJustified(_pointTargetDegrees, 6);
      Monitor.print(" err=");   _printLeftJustified(_headingError(_pointTargetDegrees), 6);
    }
  }

  void _monitorDistance(int now) {
    _distanceCm = _distance.getDistanceCm();
    if (now - _previousDistanceRead > _statusTimeSpan / 10)
    {
      // Refresh the distance reading more often than the status output.
      _distanceCm = _distanceCm ? _distanceCm : INFINITE_DISTANCE;
      _previousDistanceRead = now;
      if (!_calibration.isActive() && _pointState == POINT_IDLE) {
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

  /// @brief Feeds the current gyro rate into the calibration spin's rotation integrator and reports when it finishes.
  void _updateCalibration(int now) {
    if (!_calibration.isActive()) return;
    float ax=0,ay=0,az=0,rx=0,ry=0,rz=0;
    _movement.get(&ax, &ay, &az, &rx, &ry, &rz);
    if (_calibration.update(now, rz)) {
      _reportCalibrationFinished();
    }
  }

  /// @brief Starts a motor-driven compass calibration spin, if the required sensors and motors are ready.
  bool _startCalibration() {
    if (!(_compassOk && _movementOk && _motorsOk)) {
      show_text("e2");
      return false;
    }
    Monitor.println();
    Monitor.println("--- STARTING COMPASS CALIBRATION ---");
    Monitor.println();
    _alreadyAlertedAboutDistance = false;
    _statusTimeSpan = STATUS_TIMESPAN_WHEN_MOVING_MS;
    _calibration.start();
    return true;
  }

  /// @brief Reports the just-finished calibration spin's result to the Monitor and LED matrix.
  void _reportCalibrationFinished() {
    _statusTimeSpan = STATUS_TIMESPAN_WHEN_NOT_MOVING_MS;
    bool ok = _calibration.getLastResultOk();
    bool timedOut = _calibration.getLastTimedOut();
    float degrees = _calibration.getDegrees();

    Monitor.println();
    Monitor.println("--- FINISH COMPASS CALIBRATION ---");
    Monitor.print("spin="); Monitor.print(degrees, 2);
    Monitor.print(" deg  timeout="); Monitor.print(timedOut ? "yes" : "no");
    Monitor.print("  samples_ok="); Monitor.println(ok ? "yes" : "no");
    if (timedOut && degrees < 360.0f) {
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
    } else if (degrees < CompassCalibration::MIN_VALID_DEGREES) {
      Monitor.print("insufficient rotation: "); Monitor.print(degrees, 1);
      Monitor.print(" deg, need >= "); Monitor.println(CompassCalibration::MIN_VALID_DEGREES, 1);
    } else {
      Monitor.print("Calibration error: "); Monitor.println(_compass.getError());
    }
    Monitor.println("---------------------------");
    Monitor.flush();

    show_text(ok ? "cal" : "e3");
  }

  /// @brief Signed angular error (degrees) from the current calibrated heading to a target, normalized to (-180, 180].
  float _headingError(float targetDegrees) {
    float error = targetDegrees - _compass.getDirectionAngle();
    while (error > 180.0f) error -= 360.0f;
    while (error <= -180.0f) error += 360.0f;
    return error;
  }

  /// @brief Chooses the shorter-way turn direction for a signed heading error. turn_right increases the reported
  /// heading (confirmed empirically against real hardware), so a positive error turns right.
  const char* _turnCommandFor(float error) {
    return error >= 0 ? "turn_right" : "turn_left";
  }

  /// @brief Starts a closed-loop turn toward a target bearing, if the compass has been calibrated this boot and required sensors/motors are ready.
  bool _startPointing(const String& command, float targetDegrees) {
    if (!(_compassOk && _movementOk && _motorsOk)) {
      show_text("e5");
      return false;
    }
    if (!_calibration.hasSucceededOnce()) {
      show_text("e6");
      return false;
    }
    _pointCommand = command;
    _pointTargetDegrees = targetDegrees;
    _pointStartedAt = millis();
    _pointLastError = _headingError(targetDegrees);
    _alreadyAlertedAboutDistance = false;
    _statusTimeSpan = STATUS_TIMESPAN_WHEN_MOVING_MS;
    if (fabsf(_pointLastError) <= POINT_TOLERANCE_DEGREES) {
      // Already facing the target bearing: stop any prior motion and skip straight to settling instead of turning.
      _robotMotors.move("stop");
      _pointState = POINT_SETTLING;
      _pointSettleStartedAt = millis();
      _pointSettleSamples = 0;
    } else {
      _pointState = POINT_TURNING;
      _robotMotors.move(_turnCommandFor(_pointLastError));
    }
    return true;
  }

  /// @brief Advances a point-to-bearing turn via a turn/settle/re-check cycle (a single-pass stop would overshoot,
  /// since SmoothCompass::getDirectionAngle() lags the true heading while actively spinning), bounded by POINT_TIMEOUT_MS.
  void _updatePointing(int now) {
    if (_pointState == POINT_IDLE) return;

    if (now - _pointStartedAt >= POINT_TIMEOUT_MS) {
      _finishPointing(true);
      return;
    }

    if (_pointState == POINT_TURNING) {
      float error = _headingError(_pointTargetDegrees);
      // Guards against jumping past the tolerance band between loop iterations: a sign flip while the
      // previous error was still small means the turn just crossed the target.
      bool signFlipped = (error > 0) != (_pointLastError > 0) && fabsf(_pointLastError) < 90.0f;
      if (fabsf(error) <= POINT_TOLERANCE_DEGREES || signFlipped) {
        _robotMotors.move("stop");
        _pointState = POINT_SETTLING;
        _pointSettleStartedAt = now;
        _pointSettleSamples = 0;
      }
      _pointLastError = error;
    } else if (_pointState == POINT_SETTLING) {
      _pointSettleSamples++;
      if (now - _pointSettleStartedAt >= POINT_SETTLE_MS && _pointSettleSamples >= SmoothCompass::getSampleCount()) {
        float error = _headingError(_pointTargetDegrees);
        if (fabsf(error) <= POINT_TOLERANCE_DEGREES) {
          _finishPointing(false);
        } else {
          _pointLastError = error;
          _pointState = POINT_TURNING;
          _robotMotors.move(_turnCommandFor(error));
        }
      }
    }
  }

  /// @brief Stops the point-to-bearing turn and reports the result.
  void _finishPointing(bool timedOut) {
    _robotMotors.move("stop");
    _pointState = POINT_IDLE;
    _statusTimeSpan = STATUS_TIMESPAN_WHEN_NOT_MOVING_MS;
    float finalError = _headingError(_pointTargetDegrees);

    Monitor.println();
    Monitor.println("--- POINT TO BEARING ---");
    Monitor.print(_pointCommand); Monitor.print(" target="); Monitor.print(_pointTargetDegrees, 1);
    Monitor.print(" heading="); Monitor.print(_compass.getDirectionAngle(), 1);
    Monitor.print(" error="); Monitor.print(finalError, 1);
    Monitor.print(" timeout="); Monitor.println(timedOut ? "yes" : "no");
    Monitor.println("-------------------------");
    Monitor.flush();

    show_text(timedOut ? "e4" : "ok");
  }

  /// @brief Cancels an in-progress point-to-bearing turn without writing a result to the display (the incoming command's own code takes over).
  void _cancelPointing() {
    _robotMotors.move("stop");
    _pointState = POINT_IDLE;
    _statusTimeSpan = STATUS_TIMESPAN_WHEN_NOT_MOVING_MS;
  }

  /// @brief Cancels an in-progress compass calibration spin without writing a result to the display.
  void _cancelCalibration() {
    _calibration.cancel();
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

      _hl = (_hl == HIGH) ? LOW : HIGH;
      digitalWrite(LED_BUILTIN, _hl);
      _previousTimestamp = now;

      _showDistance();

      _showMovement();

      _showMotorStatus();

      _showCompass();

      _showCalibration();

      _showPointing();

      Monitor.println();
      Monitor.flush();
    }
  }

public:
  /// @brief Creates a sketch controller with the stationary status interval.
  SketchClass() : _calibration(_compass, _robotMotors) {
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
    _updatePointing(now);

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
    float targetDegrees = 0.0f;
    bool isBearingCommand = _bearingTargetDegrees(command, &targetDegrees);
    bool alreadyStopped = false;

    if (_calibration.isActive()) {
      if (command == "calibrate_compass") {
        // Already calibrating: ignore the repeat trigger and let the in-progress spin continue.
        return true;
      }
      _cancelCalibration();
      alreadyStopped = true;
    }

    if (_pointState != POINT_IDLE) {
      if (isBearingCommand && command == _pointCommand) {
        // Already turning toward this exact bearing: ignore the repeat trigger and let it continue.
        return true;
      }
      _cancelPointing();
      alreadyStopped = true;
    }

    if (command == "calibrate_compass") {
      return _startCalibration();
    }

    if (isBearingCommand) {
      return _startPointing(command, targetDegrees);
    }

    if (alreadyStopped && command == "stop") {
      // A cancellation above already stopped the motors (which blocks ~400ms); avoid stopping twice.
      _statusTimeSpan = STATUS_TIMESPAN_WHEN_NOT_MOVING_MS;
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
