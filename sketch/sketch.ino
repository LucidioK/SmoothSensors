#include <Arduino.h>
#include <Arduino_RouterBridge.h>
#include <Wire.h>
#include "SmoothDistance.h"
#include "SmoothMovement.h"

SmoothDistance distance;
SmoothMovement movement;
BridgeClass    bridge;

int previous;

int hl = HIGH;
bool distanceOk = false;
bool movementOk = false;
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
  distanceOk = distance.initialize();
  movementOk = movement.initialize();
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