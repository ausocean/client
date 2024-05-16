/*
NAME
  9DOF-netsender - Netsender client to send 9DOF sensor data to NetReciever.

LICENSE
  Copyright (C) 20219 the Australian Ocean Lab (AusOcean.

  This file is part of NetSender. NetSender is free software: you can
  redistribute it and/or modify it under the terms of the GNU
  General Public License as published by the Free Software
  Foundation, either version 3 of the License, or (at your option)
  any later version.

  NetSender is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with NetSender in gpl.txt. If not, see
  <http://www.gnu.org/licenses/>.
*/

#include <Wire.h>
#include <SPI.h>
#include <SparkFunLSM9DS1.h>

#include "NetSender.h"

LSM9DS1 imu;

#define LSM9DS1_M  0x1E  // Would be 0x1C if SDO_M is LOW
#define LSM9DS1_AG  0x6B // Would be 0x6A if SDO_AG is LOW

int varsum;
int currentMillis; 
int previousMillis;
                      
// DataChunk with 800 samples. At 25ms sample rate, this would equate to 20s of data,
typedef struct DataChunk {
  unsigned long timestamp;    // returned by millis()
  unsigned long samplePeriod; // 25ms (configurable)
  unsigned long nSamples;
  unsigned long buff;
  float data[800][6];         // 800 x 6 DOF samples               
};

DataChunk chunk;

//9dofReader is the reader that fills a float array with LSM9DS1 9dof data and sends this as a DataChunk
int dofReader(NetSender::Pin *pin) {
  if (pin->name[0] != 'B' && pin->name[1] != '0') {
    return -1;
  }

  int sampleCounter = 0;

  //continues to fill data array with sensor values until it is full
  while (sampleCounter < chunk.nSamples) {
    yield();
    currentMillis = millis();

    //does not record a new row until the sample period has passed
    if (currentMillis - previousMillis > chunk.samplePeriod){
      previousMillis = currentMillis;
      delay(1);
      imu.readAccel();
      imu.readMag();
      if (sampleCounter == 0){
        chunk.timestamp = millis();
      }
      chunk.data[sampleCounter][0] = imu.calcAccel(imu.ax);
      chunk.data[sampleCounter][1] = imu.calcAccel(imu.ay);
      chunk.data[sampleCounter][2] = imu.calcAccel(imu.az);
      chunk.data[sampleCounter][3] = imu.calcMag(imu.ax);
      chunk.data[sampleCounter][4] = imu.calcMag(imu.ay);
      chunk.data[sampleCounter][5] = imu.calcMag(imu.az);
      sampleCounter++; 
    }
    yield(); 
  }
  pin->value = sizeof(DataChunk);
  pin->data = (byte *)&chunk;
  return pin->value;
}

// required Arduino routines
// NB: setup runs everytime ESP8266 comes out of a deep sleep
void setup(void) {
  // set up 9dof sensor
  imu.begin();
  imu.settings.device.commInterface = IMU_MODE_I2C;
  imu.settings.device.mAddress = LSM9DS1_M;
  imu.settings.device.agAddress = LSM9DS1_AG;
  imu.setAccelScale(2); //sets the acceleration scale to +/- 2g
  
  // set up netsender
  NetSender::BinaryReader = dofReader;
  NetSender::init();
  
  chunk.nSamples = 800;
  chunk.samplePeriod = 25;
  previousMillis = millis();
  loop();
}

void loop() {
  while (!NetSender::run(&varsum)) {
    delay(RETRY_PERIOD * (long)1000);
  }
}
