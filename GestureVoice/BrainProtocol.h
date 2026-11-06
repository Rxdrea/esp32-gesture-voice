#pragma once
#include <stdint.h>
// 与桌面独立实验版区分；两端必须使用同一版本的数据格式。
constexpr uint32_t EEG_MAGIC=0x45454732;
constexpr uint8_t EEG_VERSION=2;
constexpr uint8_t EEG_FRESH=1, EEG_CONTACT=2, EEG_DECISION=4;
enum class EegCommand:uint8_t {None=0,Start=1,Stop=2};
struct __attribute__((packed)) EegPacket {
 uint32_t magic;
 uint8_t version,size,command,quality,attention,meditation,flags,channel;
 uint32_t session,sequence,sampleAgeMs;
};
static_assert(sizeof(EegPacket)==24,"脑电无线格式必须保持一致");
