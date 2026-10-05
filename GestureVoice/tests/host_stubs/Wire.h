#pragma once
#include <cstdint>
#include <cstddef>
#include <deque>
#include <map>
#include <stdexcept>
#include <vector>
class HostWire {
 public:
  struct Reply { uint8_t reg; int endStatus; size_t reported; std::vector<uint8_t> bytes; };
  std::deque<Reply> replies;
  std::map<uint8_t,uint8_t> registers;
  std::vector<uint8_t> written;
  std::deque<uint8_t> incoming;
  uint8_t address=0,reg=0;
  int sda=-1,scl=-1;
  void reset() {
    replies.clear(); registers.clear(); written.clear(); incoming.clear();
    registers[0x75]=0x68; registers[0x6B]=1; registers[0x6C]=0;
    registers[0x19]=4; registers[0x1A]=3; registers[0x1B]=0; registers[0x1C]=0;
    registers[0x3A]=0; address=reg=0;
  }
  void queue(uint8_t r,const std::vector<uint8_t>& bytes) { replies.push_back({r,0,bytes.size(),bytes}); }
  void queueFailure(uint8_t r,int error=4) { replies.push_back({r,error,0,{}}); }
  void queueShort(uint8_t r,size_t n,const std::vector<uint8_t>& bytes) { replies.push_back({r,0,n,bytes}); }
  void begin(int a,int b) { sda=a; scl=b; }
  void setClock(unsigned long) {}
  void setTimeOut(unsigned) {}
  void beginTransmission(uint8_t a) { address=a; written.clear(); }
  size_t write(uint8_t value) { written.push_back(value); return 1; }
  uint8_t endTransmission(bool stop=true) {
    if(written.empty()) return address==0x68 || address==0x3C ? 0 : 4;
    reg=written.front();
    if(!stop && !replies.empty()) {
      if(replies.front().reg!=reg) throw std::runtime_error("Unexpected I2C register order");
      if(replies.front().endStatus) { int e=replies.front().endStatus; replies.pop_front(); return static_cast<uint8_t>(e); }
    }
    if(written.size()>1) for(size_t i=1;i<written.size();++i) registers[static_cast<uint8_t>(reg+i-1)]=written[i];
    return address==0x68 ? 0 : 4;
  }
  size_t requestFrom(uint8_t,size_t count,bool=true) {
    incoming.clear();
    if(!replies.empty()) {
      if(replies.front().reg!=reg) throw std::runtime_error("Unexpected I2C data register");
      Reply r=replies.front(); replies.pop_front(); incoming.insert(incoming.end(),r.bytes.begin(),r.bytes.end()); return r.reported;
    }
    for(size_t i=0;i<count;++i) incoming.push_back(registers[static_cast<uint8_t>(reg+i)]);
    return count;
  }
  int available() const { return static_cast<int>(incoming.size()); }
  int read() { if(incoming.empty()) return -1; uint8_t v=incoming.front(); incoming.pop_front(); return v; }
};
static HostWire Wire;
