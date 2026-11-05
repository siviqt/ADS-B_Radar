#pragma once
#include <cstdio>
#include <cstring>
#include <cstdint>
// Optional enrichment failures must not replace the aircraft feed's state.
struct ServiceStatus {
    char text[3][160]={{"Waiting for home coordinates"},{"Not requested"},{"Not requested"}};
    bool set(unsigned host,const char *value) {
        if(host>=3)return false;
        bool changed=strcmp(text[host],value)!=0;snprintf(text[host],sizeof(text[host]),"%s",value);return changed;
    }
    const char *feed() const {return text[0];}
    void describe(char *out,size_t size,uint64_t now,uint64_t retry_at,bool limited,bool network_ready) const {
        if(limited && network_ready && retry_at>now)snprintf(out,size,"Feed rate limited; retry in %llus",(unsigned long long)((retry_at-now+999)/1000));
        else if(limited && network_ready)snprintf(out,size,"Aircraft feed retry scheduled");
        else snprintf(out,size,"%s",feed());
    }
};
