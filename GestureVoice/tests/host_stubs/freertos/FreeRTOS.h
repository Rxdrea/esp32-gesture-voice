#pragma once
#include <cstdint>
typedef uint32_t TickType_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define configTICK_RATE_HZ 1000
#define pdMS_TO_TICKS(ms) (ms)
#define pdPASS 1
#define portMAX_DELAY 0xffffffffu
inline void portENTER_CRITICAL(portMUX_TYPE*) {}
inline void portEXIT_CRITICAL(portMUX_TYPE*) {}
