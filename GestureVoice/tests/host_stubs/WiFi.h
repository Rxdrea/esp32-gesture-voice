#pragma once
#include "Arduino.h"
#define WL_CONNECTED 3
#define WL_DISCONNECTED 6
#define WIFI_STA 1
class WiFiClient {
 public:
  std::vector<uint8_t> bytes;
  void setTimeout(unsigned long) {}
  size_t readBytes(uint8_t* out,size_t n) { n=std::min(n,bytes.size()); std::copy(bytes.begin(),bytes.begin()+n,out); bytes.erase(bytes.begin(),bytes.begin()+n); return n; }
};
namespace host { static int wifiStatus=WL_DISCONNECTED; static unsigned wifiBegins=0; static std::function<void()> onWifiBegin; }
class HostWiFi {
 public:
  void mode(int) {}
  void begin(const char*,const char*) { ++host::wifiBegins; if(host::onWifiBegin) host::onWifiBegin(); }
  int status() const { return host::wifiStatus; }
};
static HostWiFi WiFi;
