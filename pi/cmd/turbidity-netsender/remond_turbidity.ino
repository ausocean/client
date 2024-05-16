/*
DESCRIPTION
  remond_turbidity.ino gives functionality to the Remond RS-485 Modbus-RTU
  turbidity sensor via an Arduino Uno. This file uses the ArduinoModbus
  library to send and recieve turbidity readings from hosting registers
  0x0001 and 0x0002. The readings are then converted from hexidecimal to
  a float and printed through the serial.

AUTHORS
  Harrison Telford <harrison@ausocean.org>

LICENSE
  Copyright (C) 2020-2021 the Australian Ocean Lab (AusOcean)

  It is free software: you can redistribute it and/or modify them
  under the terms of the GNU General Public License as published by the
  Free Software Foundation, either version 3 of the License, or (at your
  option) any later version.

  It is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
  for more details.

  You should have received a copy of the GNU General Public License
  along with revid in gpl.txt. If not, see http://www.gnu.org/licenses.
*/

#include <ArduinoModbus.h>

const int PRINT_DELAY  = 1000;
const int BAUD_RATE    = 9600;
const int SLAVE_ID     = 1;
const int START_REG    = 0x0001;
const int REG_NUM      = 2;

void setup() {
  Serial.begin(BAUD_RATE);
  while (!Serial);
  // Start the Modbus RTU client.
  if (!ModbusRTUClient.begin(BAUD_RATE)) {
    Serial.println("Failed to start Modbus RTU Client!");
    while (true);
  }
}

void loop() {
  // Send a holding registers read request to slave id 1, for 2 registers.
  if (!ModbusRTUClient.requestFrom(SLAVE_ID, HOLDING_REGISTERS, START_REG, REG_NUM)) {
    Serial.print("failed to read registers!");
    Serial.println(ModbusRTUClient.lastError());
  } else {
    // If the request succeeds, the sensor sends the readings, that are
    // stored in the holding registers. The read() method can be used to
    // get the raw temperature and the humidity values.
    int n1 = ModbusRTUClient.read();
    int n2 = ModbusRTUClient.read(); 

    Serial.println("-");
    Serial.println(convertHex(n1,n2));
  }
  delay(PRINT_DELAY);
}

// Converts holding register hexidecimal readings to float.
float convertHex(int n1, int n2){
  union {
      char c[4];
      float f;
    } u;
    u.c[3] = n2 >> 8;
    u.c[2] = n2;
    u.c[1] = n1 >> 8;
    u.c[0] = n1;
    return u.f;
}