// Stub for compiling without Arduino on Linux.
// gcc -fpermissive -fsyntax-only -isystem . NetSender.cpp

#ifdef __linux__

#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>

#define LOW 0
#define HIGH 1
#define F(x) x

enum PinType {
  INPUT,
  OUTPUT
};

// Types.
typedef u_int8_t uint8_t;
typedef unsigned char byte;
typedef byte* IPAddress;

class String {
public:
  String();
  String(const char*);
  String(int);
  String operator+(const String&) const;
  String operator+(const char*) const;
  String operator+=(const String&) const;
  String operator+=(const char*) const;
  bool operator==(const char*);
  bool operator!=(const char*);
  int indexOf(const String&);
  int indexOf(char, int);
  int charAt(int);
  int length();
  String substring(int);
  String substring(int, int);
  String trim();
  bool startsWith(String);
  const char* c_str();
  int toInt();
};

class SerialType {
public:
  void begin();
  void begin(int);
  void print(const char*);
  void print(const byte*);
  void print(int);
  void println(const char*);
  void println(const byte*);
  void println(int);
  void print(String);
  void println(String);
  void flush();
};

class ESPType {
public:
  void restart();
  void deepSleep(unsigned int);
};

class EEPROMType {
public:
  void begin(int);
  unsigned char read(int);
  void write(int, const unsigned char);
  void commit();
  void put(int, void*);
};

#define STATION_MODE 0
#define WL_CONNECTED 1
#define WIFI_STA 2
#define WIFI_MODE_NULL 0
#define WIFI_MODE_STA 1

class WiFiClient {
public:
  void begin();
  void begin(const char*, const char*);
  int status();
  int mode(int);
  bool connect(const char*, int);
  bool connected();
  bool available();
  bool persistent(bool);
  String SSID();
  int read(unsigned char*, size_t);
  int write(const unsigned char*, size_t);
  String readStringUntil(int);
  void print(String);
  void print(const char*);
  void disconnect();
  IPAddress localIP();
  void macAddress(byte[6]);
  void stop();
};

class HTTPClient {
public:
  void setTimeout(unsigned long);
  void begin(WiFiClient&, String);
  void addHeader(const char*, const char*);
  void collectHeaders(const char* headerNames[], int);
  String header(const char*);
  int GET();
  int POST(String);
  String getString();
  void end();
};

// Functions.
void pinMode(int, int);
int analogRead(int);
void analogWrite(int, int);
int digitalRead(int);
void digitalWrite(int, int);

unsigned long millis(); 
void delay(unsigned long);
void yield();
int random(int);
  
char* sprintf(const char*, ...);

// Low-level WiFi functions.
#define NULL_MODE 0
#define MODEM_SLEEP_T 1
#define DHCP_STOPPED 2

void wifi_set_opmode(int);
void wifi_set_sleep_type(int);
void wifi_fpm_open();
void wifi_fpm_close();
void wifi_fpm_do_sleep(int);
void wifi_fpm_do_wakeup();
void wifi_station_connect();
void wifi_station_disconnect();
int wifi_station_get_connect_status();

// Globals.
SerialType Serial;
WiFiClient WiFi;
ESPType ESP;
EEPROMType EEPROM;

#endif
