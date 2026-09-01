#include <Arduino.h>
#include <Arduino_RouterBridge.h>
#include <Arduino_Modulino.h>
#include <Wire.h>
#include "SmoothDistance.h"
#include "SmoothMovement.h"
#include "LedMatrixDisplay.h"
#include "RobotMotors.h"

#define INFINITE_DISTANCE 1000000
SmoothDistance distance;
SmoothMovement movement;
LedMatrixDisplay ledMatrix;
RobotMotors robotMotors;

int previous;
int previousDistanceRead;

int hl = HIGH;
bool distanceOk = false;
bool movementOk = false;
int distanceCm = INFINITE_DISTANCE;

bool show_text(String text)
{
  ledMatrix.print(text.c_str());
  return true;
}

bool move(String command)
{
  return robotMotors.move(command);
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
  Bridge.begin();
  Bridge.provide("show_text", show_text);
  Bridge.provide("move", move);
  distanceOk = distance.initialize();
  movementOk = movement.initialize();
  ledMatrix.initialize();
  robotMotors.initialize();
}

void showDistance() {
    if (distanceOk)
    {
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

void showMotorStatus() {
  Monitor.print("Motor: ");
  Monitor.println(robotMotors.getStatus());
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

  if (now - previousDistanceRead > 100)
  {
    distanceCm = distance.getDistanceCm();
    distanceCm = distanceCm ? distanceCm : INFINITE_DISTANCE;
    previousDistanceRead = now;
    if (distanceCm < 10) {
      move("stop");
      show_text("STP");
    }
  }

  if (timespan > 1000)
  {
    Monitor.flush();
    hl = (hl == HIGH) ? LOW : HIGH;
    digitalWrite(LED_BUILTIN, hl);
    previous = now;

    showDistance();

    showMovement();

    showMotorStatus();
    
    Monitor.flush();
  }
}
