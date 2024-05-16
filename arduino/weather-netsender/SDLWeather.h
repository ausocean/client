 /*
  SDLWeather.h - Library for Weather Sensor
  Designed for:  SwitchDoc Labs WeatherRack www.switchdoc.com
  Argent Data Systems
  SparkFun Weather Station Meters
  Created by SwitchDoc Labs July 27, 2014.
  Released into the public domain.
    Version 1.1 - updated constants to suppport 3.3V
    Version 1.6 - Support for ADS1015 in WeatherPiArduino Board February 7, 2015
*/
#ifndef SDLWeather_h
#define SDLWeather_h

// sample mode means return immediately.  The wind speed is averaged over samplePeriod or when you ask, whichever is longer
#define SDL_MODE_SAMPLE 0
// Delay mode means to wait for samplePeriod and the average after that time.
#define SDL_MODE_DELAY 1

#include "Arduino.h"

extern "C" void serviceInterruptAnem(void)  __attribute__ ((signal));
extern "C" void serviceInterruptRain(void)  __attribute__ ((signal));
class SDLWeather
{
  public:
  SDLWeather(int pinAnem, int pinRain, int ADChannel);
  
  float getCurrentRainTotal();
  float getWindSpeed();
  float getWindDirection();
  float getWindGust();
  void setWindMode(int selectedMode, float samplePeriod);
  
  static unsigned long _shortestWindTime;
  static long _currentRainCount;
  static long _currentWindCount;
    
  friend void serviceInterruptAnem();
  friend void serviceInterruptRain(); 
  
  private:
  int _pinAnem;
  int _pinRain;    
  int _ADChannel;
  float _samplePeriod;
  int _selectedMode;
    
  unsigned long _startSampleTime;

  float _currentWindSpeed;
  float _currentWindDirection;
    
  void startWindSample(float sampleTime);
  float getCurrentWindSpeedWhenSampling();
};

#endif

