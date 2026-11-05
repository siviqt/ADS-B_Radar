#pragma once
#include <stdint.h>
extern uint64_t test_time;
inline int64_t esp_timer_get_time() {return test_time*1000;}
