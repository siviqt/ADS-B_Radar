#pragma once
#include "map_pack.h"
#include "lvgl.h"
namespace adsb_maps {
void start();
void network_ready(bool connected);
void radio_initialized();
void sd_event(bool present);
void request_sd();
// UI-thread API. A returned descriptor lives until the next call; no I/O on this thread.
const lv_image_dsc_t *image(Request request,bool enabled,Request *display=nullptr,bool tracking=false);
const char *attribution();
const char *status();
}
