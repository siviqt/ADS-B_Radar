#pragma once
#include <algorithm>
#include <cstdint>
// Session-only adaptation: do not resume a fast burst after server cooldown.
struct FeedPacing {
    unsigned floor_seconds=0,failures=0;
    unsigned interval(unsigned configured) const {return std::max(configured,floor_seconds);}
    void throttle(unsigned configured) {floor_seconds=std::max(15u,std::min(300u,interval(configured)*2));}
    uint64_t next(bool ok,unsigned configured,uint64_t now,uint64_t cooldown) {
        failures=ok?0:std::min(6u,failures+1);
        unsigned delay=std::max(interval(configured),ok?0u:5u<<failures);
        return std::max(now+(uint64_t)delay*1000,cooldown);
    }
};
