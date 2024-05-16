/*
 * Modified version of SwitchDoc Labs WeatherRack Library for ESP8266:
 * - Cleaned up variables names, removed dead code, made naming consistent (CamelCase)
 * - Reduced wind vane from 16 to 8 positions and recalibrated (YMMV)
 * - Removed ADMode
 * 
 * Original description: 
 * SDLWeather.cpp - Library for SwitchDoc Labs WeatherRack.
 *  SparkFun Weather Station Meters
 * Argent Data Systems
 * Created by SwitchDoc Labs July 27, 2014.
 * Released into the public domain.
 *   Version 1.1 - updated constants to suppport 3.3V
 *   Version 1.6 - Support for ADS1015 in WeatherPiArduino Board February 7, 2015
 */

#include "Arduino.h"
#include "SDLWeather.h"

#define DEBUG false
#define ADC_VOLTAGE 1.0       // Volts
#define ADC_V_DIVIDE 0.327    // voltage divider ratio on ADC
#define RAIN_FACTOR 0.2794    // rain bucket sensor
#define WIND_FACTOR 2.400     // anemometer sensor
#define VANE_TOLERANCE 0.05   // Volts

// globals updated by interrupt handlers
volatile unsigned long LastWindTime;
volatile unsigned long LastRainTime;

// static members updated by interrupt handlers
long SDLWeather::_currentWindCount = 0;
long SDLWeather::_currentRainCount = 0;
unsigned long SDLWeather::_shortestWindTime = 0;

SDLWeather::SDLWeather(int pinAnem, int pinRain, int ADChannel) {
  _pinAnem = pinAnem;
  _pinRain = pinRain;
  _ADChannel = ADChannel;

  _currentRainCount = 0;
  _currentWindCount = 0;
  _currentWindSpeed = 0.0;
  _currentWindDirection = 0;
  _shortestWindTime = 0xffffffff;
  _samplePeriod = 5.0;
  _selectedMode = SDL_MODE_SAMPLE;
  _startSampleTime = micros();
   
   // set up interrupts
  pinMode(_pinAnem, INPUT_PULLUP);  // pin for anenometer interrupts
  pinMode(_pinRain, INPUT_PULLUP);  // pin for rain interrupts
  LastWindTime = 0;
  LastRainTime = 0;
  attachInterrupt(digitalPinToInterrupt(_pinAnem), serviceInterruptAnem, RISING);
  attachInterrupt(digitalPinToInterrupt(_pinRain), serviceInterruptRain, RISING);
}

float SDLWeather::getCurrentRainTotal() {
  float rain_amount = RAIN_FACTOR * _currentRainCount / 2;  // mm of rain - we get two interrupts per bucket
  _currentRainCount = 0;
  return rain_amount;
}

float SDLWeather::getWindSpeed() { // in milliseconds
  if (_selectedMode == SDL_MODE_SAMPLE) {
    _currentWindSpeed = getCurrentWindSpeedWhenSampling();
  } else {
    // km/h * 1000 msec
    _currentWindCount = 0;
    delay(_samplePeriod * 1000);
    _currentWindSpeed = ((float)_currentWindCount/_samplePeriod) * WIND_FACTOR;
   }
  return _currentWindSpeed;
}

float SDLWeather::getWindGust() {
  unsigned long latestTime = _shortestWindTime; // elapsed
  _shortestWindTime=0xffffffff;  // reset
  double time = latestTime/1000000.0;  // in microseconds
  return (1/(time)) * WIND_FACTOR / 2; 
}

int voltageToVane(float value) {
  // NB: the wind vane is NOT reliable for 16 positions, so just use 8 positions
  float vaneVoltage[8] = {0.78, 1.27, 1.59, 1.52, 1.44, 1.05, 0.35, 0.57}; // determined empirically in DEBUG mode
  // corresponding degrees:  0    45    90   135   180   225   270   315
  if (DEBUG) Serial.print("("), Serial.print(value), Serial.print("V) ");
  for (int ii = 0; ii < 8; ii++) {
    if ((value > vaneVoltage[ii] - VANE_TOLERANCE) && (value < vaneVoltage[ii] + VANE_TOLERANCE)) {
      if (DEBUG) Serial.print("("), Serial.print(ii), Serial.print(") ");
      return ii;
    }
  }
  return -1; // wind vane lookup failed
}

float SDLWeather::getWindDirection() {
  float voltageValue = (analogRead(_ADChannel)/1023.0) * ADC_VOLTAGE / ADC_V_DIVIDE;
  int vane = voltageToVane(voltageValue);
  if (vane == -1) {
    return _currentWindDirection;
  } else {  
    return vane * 45.0;
  }
}

// ongoing samples in wind
void SDLWeather::startWindSample(float samplePeriod) {
  _startSampleTime = micros();
  _samplePeriod = samplePeriod;
}

float SDLWeather::getCurrentWindSpeedWhenSampling() {
  unsigned long compareValue = _samplePeriod * 1000000;
  
  if (micros() - _startSampleTime >= compareValue) {
    // sample time exceeded, calculate currentWindSpeed
    float _timeSpan;
    _timeSpan = (micros() - _startSampleTime);
    _currentWindSpeed = ((float)_currentWindCount/(_timeSpan)) * WIND_FACTOR * 1000000;
    _currentWindCount = 0;
    _startSampleTime = micros();
  } // else return current wind speed
  return _currentWindSpeed;
}

void SDLWeather::setWindMode(int selectedMode, float samplePeriod) {
  _samplePeriod = samplePeriod; // in seconds
  _selectedMode = selectedMode;
  
  if (_selectedMode == SDL_MODE_SAMPLE) {
    startWindSample(_samplePeriod);
  }
}

// interrupt handlers, which mostly just update counts
void serviceInterruptAnem() {
  // Side effects: updates SDLWeather::_currentWindCount and SDLWeather::_shortestWindTime
  unsigned long elapsedTime = (unsigned long)(micros() - LastWindTime);

  LastWindTime = micros();
  if (elapsedTime > 1000) {  // debounce 
    SDLWeather::_currentWindCount++;
    if (elapsedTime < SDLWeather::_shortestWindTime) {
     SDLWeather::_shortestWindTime = elapsedTime;
    }
  }
}

void serviceInterruptRain() {
  // Side effects: updates SDLWeather::_currentRainCount++;
  unsigned long elapsedTime = (unsigned long) (micros()-LastRainTime);
  LastRainTime = micros();
  if (elapsedTime > 500) {  // debounce
    SDLWeather::_currentRainCount++;
  }  
}
