#include "ui/sd_storage.h"
#include <cassert>
#include <cstring>
#include <vector>

static std::vector<char> calls;
static esp_err_t mount_result=ESP_OK,unmount_result=ESP_OK;
extern "C" esp_err_t bsp_storage_mount_advanced(bsp_storage_type_t type,const char *root,int files,bool format) {
    assert(type==BSP_STORAGE_TYPE_SDCARD && std::strcmp(root,"/sd")==0);
    assert(files==6 && !format);
    calls.push_back('M');return mount_result;
}
extern "C" esp_err_t bsp_storage_unmount(bsp_storage_type_t type) {
    assert(type==BSP_STORAGE_TYPE_SDCARD);calls.push_back('U');return unmount_result;
}
int main() {
    adsb_maps::SdStorage sd;
    // Neither offline AP state nor UI visibility enters the controller; the
    // initialized hosted controller and physical card are the only prerequisites.
    assert(!sd.mount(false,true));assert(!sd.mount(true,false));assert(calls.empty());
    assert(!sd.attempted());
    mount_result=ESP_FAIL;
    assert(!sd.mount(true,true));assert(sd.attempted());
    assert(!sd.mount(true,true));assert(calls==std::vector<char>{'M'});
    sd.reset();mount_result=ESP_OK;
    assert(sd.mount(true,true));assert(sd.mount(true,true));
    assert((calls==std::vector<char>{'M','M'}));
    // Removal closes readers outside the controller, then requests cleanup.
    // No unmount may run before hosted-radio initialization.
    sd.reset();assert(!sd.mount(false,false));assert(calls.size()==2);
    assert(!sd.mount(true,false));assert(calls.back()=='U');
    assert(!sd.mount(true,false));assert(calls.size()==3);
    assert(sd.mount(true,true));assert(calls.back()=='M');
    // Failed cleanup prevents a second mount and requires an explicit retry.
    sd.reset();unmount_result=ESP_FAIL;
    assert(!sd.mount(true,true));assert(sd.attempted());
    assert(!sd.mount(true,true));assert(calls.size()==5);
    sd.reset();unmount_result=ESP_OK;
    assert(sd.mount(true,true));
    assert((calls==std::vector<char>{'M','M','U','M','U','U','M'}));
    // Repeated retry cycles always unmount before mounting and never format.
    for(int i=0;i<100;i++) {
        sd.reset();assert(sd.mount(true,true));
        assert(calls[calls.size()-2]=='U' && calls.back()=='M');
    }
    // Launcher return releases the actual mounted BSP slot. Failure is reported
    // and retriable, without allowing another mount during failed cleanup.
    unmount_result=ESP_FAIL;assert(!sd.release(true));
    assert(!sd.mount(true,true));
    unmount_result=ESP_OK;assert(sd.release(true));
    size_t count=calls.size();assert(sd.release(true));assert(calls.size()==count);
    adsb_maps::SdStorage unused;assert(unused.release(false));
}
