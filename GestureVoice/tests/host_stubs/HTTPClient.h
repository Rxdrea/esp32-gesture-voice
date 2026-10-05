#pragma once
#include "WiFi.h"
namespace host {
struct HttpReply { int status; std::string body; std::vector<uint8_t> audio; };
struct HttpRequest { std::string method,url,body; };
static std::deque<HttpReply> httpReplies;
static std::vector<HttpRequest> httpRequests;
static std::function<void()> onHttp;
static unsigned httpBegins=0,httpEnds=0;
inline void reply(int status,const std::string& body="",const std::vector<uint8_t>& audio={}) { httpReplies.push_back({status,body,audio}); }
}
// No socket or DNS API exists in this boundary. All replies are local fixtures.
class HTTPClient {
  std::string url_;
  host::HttpReply reply_{503,"",{}};
  WiFiClient stream_;
  int send(const char* method,const String& body) {
    host::httpRequests.push_back({method,url_,body.c_str()});
    if(host::httpReplies.empty()) reply_={503,"",{}};
    else { reply_=host::httpReplies.front(); host::httpReplies.pop_front(); }
    stream_.bytes=reply_.audio;
    if(host::onHttp) host::onHttp();
    return reply_.status;
  }
 public:
  bool begin(WiFiClient&,const String& url) { url_=url.c_str(); ++host::httpBegins; return true; }
  void setTimeout(unsigned long) {}
  void addHeader(const char*,const String&) {}
  int POST(const String& body) { return send("POST",body); }
  int GET() { return send("GET",""); }
  String getString() { return String(reply_.body); }
  int getSize() { return static_cast<int>(reply_.audio.size()); }
  WiFiClient* getStreamPtr() { return &stream_; }
  void end() { ++host::httpEnds; }
};
