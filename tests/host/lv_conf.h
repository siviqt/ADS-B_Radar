#pragma once
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_DEFAULT &lv_font_montserrat_18
#define LV_USE_OS LV_OS_NONE
#define LV_USE_LOG 0
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#ifndef __ASSEMBLER__
#include <stdlib.h>
#endif
#define LV_ASSERT_HANDLER abort();
#define LV_USE_SYSMON 0
