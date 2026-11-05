#pragma once
#include <stddef.h>
// Transport-independent byte accounting, shared with host replay checks.
struct BodyBounds {
    size_t limit, used=0;
    bool append(size_t count) {
        if(count>limit-used) return false;
        used+=count; return true;
    }
    bool complete(bool transport_complete, long long declared, bool drained=true) const {
        return drained && transport_complete && (declared<=0 || used==(size_t)declared);
    }
};
