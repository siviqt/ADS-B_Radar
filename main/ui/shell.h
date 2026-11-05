#pragma once
#include "bsp/input.h"
#ifdef __cplusplus
extern "C" {
#endif
void adsb_ui_init(bool services_ready);
void adsb_ui_input(const bsp_input_event_t *event);
void adsb_ui_tick(void);
void adsb_ui_network(const char *text);
bool adsb_ui_take_reconnect(void);
bool adsb_ui_take_exit(void);
#ifdef __cplusplus
}
#endif
