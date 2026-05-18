#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>

Adafruit_BMP280 bmp;

const int mqPin = A5;
const int PIR_PIN = 7;
const int FLAME_ANALOG_PIN = A2;
const int MCP9700_PIN = A0;  // MCP9700 temperatur sensor

void setup() {
  SerialUSB.begin(115200);
  delay(2000); // giver tid til USB at starte

  if (!bmp.begin(0x76)) {
    SerialUSB.println("BMP280 fejl");
  }

  pinMode(PIR_PIN, INPUT);

  SerialUSB.println("temp_bmp,temp_mcp,mq9,flameAnalog,pir,label");
}

void loop() {
  // BMP280 temperatur
  float temp_bmp = bmp.readTemperature();

  // MCP9700 temperatur
  int raw = analogRead(MCP9700_PIN);
  float voltage = raw * (3.3 / 1023.0);
  float temp_mcp = (voltage - 0.5) / 0.01;  // MCP9700 formel

  int mq9 = analogRead(mqPin);
  int flameAnalog = analogRead(FLAME_ANALOG_PIN);
  int pir = digitalRead(PIR_PIN);

  // OPTION 2 – mere forståeligt output
  SerialUSB.print("BMP280 Temp: ");
  SerialUSB.print(temp_bmp);
  SerialUSB.print("°C, MCP9700 Temp: ");
  SerialUSB.print(temp_mcp);
  SerialUSB.print("°C, MQ9: ");
  SerialUSB.print(mq9);
  SerialUSB.print(", Flame: ");
  SerialUSB.print(flameAnalog);
  SerialUSB.print(", PIR: ");
  SerialUSB.print(pir);
  SerialUSB.print(", Label: ");
  SerialUSB.println("normal");

  delay(1000);
}

