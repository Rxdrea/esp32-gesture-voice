#pragma once
// Host-only boundary: no physical IO and no network implementation.
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <functional>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>
using std::size_t;
using std::isfinite;
#define CONFIG_IDF_TARGET_ESP32 1
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define ADC_11db 3
#define SERIAL_8N1 0x800001c
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#define ARDUINOJSON_ENABLE_ARDUINO_STREAM 0
#define ARDUINOJSON_ENABLE_ARDUINO_PRINT 0
#define ARDUINOJSON_ENABLE_PROGMEM 0
class String {
  std::string value_;
 public:
  String()=default;
  String(const char* value):value_(value?value:"") {}
  String(const std::string& value):value_(value) {}
  String(char value):value_(1,value) {}
  String(int value):value_(std::to_string(value)) {}
  String(unsigned value):value_(std::to_string(value)) {}
  String(long value):value_(std::to_string(value)) {}
  String(unsigned long value):value_(std::to_string(value)) {}
  const char* c_str() const { return value_.c_str(); }
  size_t length() const { return value_.length(); }
  bool isEmpty() const { return value_.empty(); }
  bool reserve(size_t n) { value_.reserve(n); return true; }
  bool concat(const char* s) { value_+=s?s:""; return true; }
  bool concat(const char* s,size_t n) { value_.append(s,n); return true; }
  String& operator=(const char* s) { value_=s?s:""; return *this; }
  String& operator+=(const String& s) { value_+=s.value_; return *this; }
  String& operator+=(char c) { value_+=c; return *this; }
  char operator[](size_t i) const { return value_[i]; }
  char& operator[](size_t i) { return value_[i]; }
  void trim() {
    const size_t a=value_.find_first_not_of(" \t\r\n");
    if(a==std::string::npos) { value_.clear(); return; }
    const size_t b=value_.find_last_not_of(" \t\r\n"); value_=value_.substr(a,b-a+1);
  }
  int indexOf(char c) const { auto p=value_.find(c); return p==std::string::npos?-1:static_cast<int>(p); }
  int lastIndexOf(char c) const { auto p=value_.rfind(c); return p==std::string::npos?-1:static_cast<int>(p); }
  bool startsWith(const char* s) const { return value_.rfind(s,0)==0; }
  String substring(size_t start) const { return start>=length()?String():String(value_.substr(start)); }
  String substring(size_t start,size_t end) const { return start>=length()?String():String(value_.substr(start,end-start)); }
  void remove(size_t start) { if(start<length()) value_.erase(start); }
  void remove(size_t start,size_t n) { if(start<length()) value_.erase(start,n); }
  friend String operator+(const String& a,const String& b) { return String(a.value_+b.value_); }
  friend bool operator==(const String& a,const String& b) { return a.value_==b.value_; }
  friend bool operator!=(const String& a,const String& b) { return !(a==b); }
  friend std::ostream& operator<<(std::ostream& s,const String& v) { return s<<v.value_; }
};
namespace host {
static uint32_t nowUs=0;
static std::vector<int> analogPins,digitalPins,attenuationPins;
static std::vector<std::pair<int,int>> pinModes,digitalWrites;
static int analogValue=512,digitalValue=HIGH;
static unsigned delayCalls=0;
static std::function<void(uint32_t)> onDelay;
inline void setMillis(uint32_t ms) { nowUs=ms*1000u; }
inline void advanceMillis(uint32_t ms) { nowUs+=ms*1000u; }
}
inline uint32_t millis() { return host::nowUs/1000u; }
inline uint32_t micros() { return host::nowUs; }
inline void delay(uint32_t ms) { ++host::delayCalls; if(host::delayCalls>10000) throw std::runtime_error("setup or operation waits forever"); host::advanceMillis(ms); if(host::onDelay) host::onDelay(ms); }
inline void digitalWrite(uint8_t pin,int v) { host::digitalWrites.emplace_back(pin,v); }
inline int digitalRead(uint8_t pin) { host::digitalPins.push_back(pin); return host::digitalValue; }
inline int analogRead(uint8_t pin) { host::analogPins.push_back(pin); return host::analogValue; }
inline void pinMode(uint8_t pin,int mode) { host::pinModes.emplace_back(pin,mode); }
inline void analogReadResolution(int) {}
inline void analogSetPinAttenuation(uint8_t pin,int) { host::attenuationPins.push_back(pin); }
class HostSerial {
 public:
  std::string output;
  std::deque<char> input;
  void begin(unsigned long) {}
  void begin(unsigned long,int,int,int) {}
  int available() const { return static_cast<int>(input.size()); }
  int read() { if(input.empty()) return -1; char c=input.front(); input.pop_front(); return c; }
  void feed(const std::string& s) { input.insert(input.end(),s.begin(),s.end()); }
  void clearOutput() { output.clear(); }
  size_t printf(const char* format,...) {
    va_list args; va_start(args,format); va_list copy; va_copy(copy,args);
    const int n=std::vsnprintf(nullptr,0,format,copy); va_end(copy);
    if(n<0) { va_end(args); return 0; }
    std::vector<char> b(static_cast<size_t>(n)+1); std::vsnprintf(b.data(),b.size(),format,args); va_end(args);
    output.append(b.data(),static_cast<size_t>(n)); return static_cast<size_t>(n);
  }
  template<class T> void print(const T& x) { std::ostringstream s; s<<x; output+=s.str(); }
  void print(float x,int n) { printf("%.*f",n,static_cast<double>(x)); }
  void print(double x,int n) { printf("%.*f",n,x); }
  void println() { output+='\n'; }
  template<class T> void println(const T& x) { print(x); println(); }
  size_t write(uint8_t c) { output.push_back(static_cast<char>(c)); return 1; }
};
static HostSerial Serial;
class HardwareSerial : public HostSerial {
 public:
  explicit HardwareSerial(int) {}
};
