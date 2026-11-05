#pragma once
#include <ArduinoJson.h>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <memory>
#include <new>

namespace adsb_maps {
constexpr unsigned width=800,height=418,bytes=width*height*2;
struct Request {double lat,lon;unsigned range;};
// Accept sub-meter settings serialization differences; never substitute a different viewport.
inline bool same(Request a,Request b) {return a.range==b.range && fabs(a.lat-b.lat)<1e-5 && fabs(remainder(a.lon-b.lon,360.))<1e-5;}
inline uint32_t crc32(const uint8_t *p,size_t n) {
    uint32_t crc=~0u;while(n--){crc^=*p++;for(int b=0;b<8;b++)crc=(crc>>1)^(0xedb88320u&-(crc&1));}return ~crc;
}
// Bounded metadata, fixed canonical filenames and exact file sizes. No paths from JSON are opened.
inline bool load(const char *root,Request request,uint8_t *pixels,char *credit,size_t credit_size) {
    std::string base=std::string(root)+"/maps/";
    FILE *f=fopen((base+"manifest.json").c_str(),"rb");if(!f)return false;
    std::unique_ptr<char[]> json(new(std::nothrow) char[8193]);
    if(!json){fclose(f);return false;}
    size_t n=fread(json.get(),1,8193,f);bool bad=ferror(f);fclose(f);
    if(bad || n==8193)return false;
    ArduinoJson::JsonDocument doc;
    if(ArduinoJson::deserializeJson(doc,json.get(),n))return false;
    auto d=doc.as<ArduinoJson::JsonObject>();
    if(d["version"]!=1 || d["width"]!=width || d["height"]!=height ||
       d["projection"]!="local-equirectangular-v1" || d["format"]!="rgb565le" ||
       !d["center_lat"].is<double>() || !d["center_lon"].is<double>() ||
       !d["attribution"].is<const char*>() || !*d["attribution"].as<const char*>())return false;
    double lat=d["center_lat"],lon=d["center_lon"];
    if(!std::isfinite(lat)||!std::isfinite(lon)||fabs(lat)>80||fabs(lon)>180||!same(request,{lat,lon,request.range}))return false;
    auto maps=d["maps"].as<ArduinoJson::JsonArray>();if(maps.size()>6)return false;
    for(auto entry:maps) {
        if(entry["range_nm"]!=request.range)continue;
        if(entry["bytes"]!=bytes || !entry["crc32"].is<uint32_t>())return false;
        f=fopen((base+std::to_string(request.range)+".rgb565").c_str(),"rb");if(!f)return false;
        n=fread(pixels,1,bytes,f);int extra=fgetc(f);bad=ferror(f);fclose(f);
        if(n!=bytes || extra!=EOF || bad || crc32(pixels,n)!=entry["crc32"].as<uint32_t>())return false;
        auto attribution=d["attribution"].as<const char*>();
        if(strlen(attribution)>=credit_size)return false;
        snprintf(credit,credit_size,"%s",attribution);return true;
    }
    return false;
}
}
