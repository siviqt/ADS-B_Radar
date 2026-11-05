#pragma once

extern "C" {
#include "bsp/storage.h"
}

namespace adsb_maps {
// Only the atlas worker may call this controller. Close all readers before reset().
// The BSP owns SD power, card/slot allocation and the shared-host unmount workaround.
class SdStorage {
    bool mounted_ = false;
    bool attempted_ = false;
    bool reset_ = false;
public:
    void reset() { reset_ = true; attempted_ = false; }
    bool attempted() const { return attempted_; }
    bool release(bool radio_ready) {
        reset();
        mount(radio_ready, false);
        return !mounted_;
    }
    bool mount(bool radio_ready, bool present) {
        if (!radio_ready) return false;
        if (reset_) {
            // A failed unmount must never be followed by a second mount. Wait for
            // another explicit retry or card event before retrying cleanup.
            if (attempted_) return false;
            if (mounted_) {
                if (bsp_storage_unmount(BSP_STORAGE_TYPE_SDCARD) != ESP_OK) {
                    attempted_ = true;
                    return false;
                }
                mounted_ = false;
            }
            reset_ = false;
        }
        if (!present) return false;
        if (mounted_) return true;
        if (attempted_) return false;
        attempted_ = true;
        mounted_ = bsp_storage_mount_advanced(BSP_STORAGE_TYPE_SDCARD, "/sd", 6, false) == ESP_OK;
        return mounted_;
    }
};
}
