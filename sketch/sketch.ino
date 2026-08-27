#include <Arduino.h>
#include <Arduino_RouterBridge.h>
#include <Arduino_Modulino.h>
#include <Wire.h>
#include "SmoothDistance.h"
#include "SmoothMovement.h"
#include "LedMatrixDisplay.h"

SmoothDistance distance;
SmoothMovement movement;
BridgeClass    bridge;
LedMatrixDisplay ledMatrix;
ModulinoMotors motors;

int previous;

int hl = HIGH;
bool distanceOk = false;
bool movementOk = false;
bool motorsOk = false;

const uint8_t DRIVE_SPEED = 50;

bool show_text(String text)
{
  ledMatrix.print(text.c_str());
  return true;
}

// Motor A drives the left wheel, Motor B the right wheel.
bool move(String command)
{
  if (!motorsOk) return false;

  if (command == "go_ahead")
  {
    motors.setInvertA(false);
    motors.setInvertB(false);
    motors.setSpeedA(DRIVE_SPEED);
    motors.setSpeedB(DRIVE_SPEED);
  }
  else if (command == "turn_right")
  {
    motors.setInvertA(false);
    motors.setInvertB(true);
    motors.setSpeedA(DRIVE_SPEED);
    motors.setSpeedB(DRIVE_SPEED);
  }
  else if (command == "turn_left")
  {
    motors.setInvertA(true);
    motors.setInvertB(false);
    motors.setSpeedA(DRIVE_SPEED);
    motors.setSpeedB(DRIVE_SPEED);
  }
  else if (command == "stop")
  {
    motors.stop();
  }
  else
  {
    return false;
  }
  return true;
}

void setup() {
  Wire.begin();
  delay(500);
  Monitor.begin(9600);
  delay(1000);
  Monitor.println("\n\n\n===========================\nSmoothSensors003...");
  Monitor.flush();
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, hl);
  previous = millis();
  bridge.begin();
  bridge.provide("show_text", show_text);
  bridge.provide("move", move);
  distanceOk = distance.initialize();
  movementOk = movement.initialize();
  ledMatrix.initialize();
  motorsOk = motors.begin();
  motors.setStepperModeEnabled(false);
}

void showDistance() {
    if (distanceOk)
    {
      int distanceCm = distance.getDistanceCm();
      Monitor.print("Distance: ");
      Monitor.print(distanceCm);
      Monitor.println("cm");
    }
    else
    {
      Monitor.println("Distance NOK");
    }  
}

void showMovement() {
  if (movementOk) {
    float ax=0,ay=0,az=0,rx=0,ry=0,rz=0;
    movement.get(&ax, &ay, &az, &rx, &ry, &rz);
    Monitor.print("ax=");
    Monitor.print(ax);
    Monitor.print(" ay=");
    Monitor.print(ay);
    Monitor.print(" az=");
    Monitor.print(az);
    Monitor.print(" rx=");
    Monitor.print(rx);
    Monitor.print(" ry=");
    Monitor.print(ry);
    Monitor.print(" rz=");
    Monitor.println(rz);

  }
  else
  {
    Monitor.println("Movement NOK");
  }  
}


void loop() {
  // put your main code here, to run repeatedly:
  int now = millis();
  int timespan = now - previous;

  if (distanceOk) {
    distance.record();
  }

  if (movementOk) {
    movement.record();
  }
  
  if (timespan > 1000)
  {
    Monitor.flush();
    hl = (hl == HIGH) ? LOW : HIGH;
    digitalWrite(LED_BUILTIN, hl);
    previous = now;

    showDistance();

    showMovement();
    
    Monitor.flush();
  }
}


RpcCall get_sensor_values()
{
  
}