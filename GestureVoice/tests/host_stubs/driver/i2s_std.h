#pragma once
#include "Arduino.h"
#define ESP_OK 0
#define I2S_NUM_0 0
#define I2S_ROLE_MASTER 0
#define I2S_GPIO_UNUSED -1
#define I2S_DATA_BIT_WIDTH_16BIT 16
#define I2S_SLOT_MODE_STEREO 2
#define I2S_CHANNEL_DEFAULT_CONFIG(port,role) {port,role}
#define I2S_STD_CLK_DEFAULT_CONFIG(rate) {rate}
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits,mode) {bits,mode}
typedef int gpio_num_t;
typedef void* i2s_chan_handle_t;
struct i2s_chan_config_t { int port,role; };
struct i2s_std_config_t {
  struct { int rate; } clk_cfg;
  struct { int bits,mode; } slot_cfg;
  struct { int mclk,bclk,ws,dout,din; struct { bool mclk_inv,bclk_inv,ws_inv; } invert_flags; } gpio_cfg;
};
namespace host {
static int i2sCreateResult=ESP_OK,i2sInitResult=ESP_OK;
static unsigned i2sCreates=0,i2sWrites=0,i2sDeletes=0;
static i2s_std_config_t i2sConfig={};
static std::vector<int16_t> playedSamples;
static std::function<void()> onI2sWrite;
}
inline int i2s_new_channel(const i2s_chan_config_t*,i2s_chan_handle_t* out,void*) { ++host::i2sCreates; *out=reinterpret_cast<void*>(1); return host::i2sCreateResult; }
inline int i2s_channel_init_std_mode(i2s_chan_handle_t,const i2s_std_config_t* c) { host::i2sConfig=*c; return host::i2sInitResult; }
inline int i2s_channel_enable(i2s_chan_handle_t) { return ESP_OK; }
inline int i2s_channel_disable(i2s_chan_handle_t) { return ESP_OK; }
inline int i2s_del_channel(i2s_chan_handle_t) { ++host::i2sDeletes; return ESP_OK; }
inline int i2s_channel_write(i2s_chan_handle_t,const void* data,size_t n,size_t* written,uint32_t) {
  ++host::i2sWrites; *written=n; const int16_t* p=static_cast<const int16_t*>(data);
  host::playedSamples.insert(host::playedSamples.end(),p,p+n/2); if(host::onI2sWrite) host::onI2sWrite(); return ESP_OK;
}
