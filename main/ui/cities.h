#pragma once
#include "atlas.h"

namespace adsb_cities {
struct City {uint32_t id=0;double lat=0,lon=0;uint32_t population=0;char name[48]={},country[40]={},region[12]={};};
struct Results {uint32_t token=0;unsigned count=0;bool more=false,available=false;City cities[20];};
inline std::string normalized(const char *query) {
    std::string out;bool space=false;
    for(size_t i=0;query[i] && i<128;i++) {
        unsigned char c=query[i];if(c<32 || c>126)return {};
        if(c==' '){space=!out.empty();continue;}if(space){out+=' ';space=false;}out+=char(std::tolower(c));
        if(out.size()>63)return {};
    }
    return out;
}
class Database {
    adsb_atlas::File index,data;uint32_t names=0,records=0;
    bool entry(uint32_t row,uint8_t *out) {
        return row<names && !fseek(index.p,32+long(row)*80,SEEK_SET) && adsb_atlas::exact(index.p,out,80) &&
            adsb_maps::crc32(out,76)==adsb_atlas::u32(out+76) && memchr(out,0,64) && adsb_atlas::u32(out+64)<records;
    }
    bool city(uint32_t row,City &out) {
        uint8_t b[120];if(row>=records || fseek(data.p,16+long(row)*120,SEEK_SET)||!adsb_atlas::exact(data.p,b,120)||adsb_maps::crc32(b,116)!=adsb_atlas::u32(b+116))return false;
        if(!memchr(b+16,0,48)||!memchr(b+64,0,40)||!memchr(b+104,0,12))return false;
        out={};out.id=adsb_atlas::u32(b);out.lat=adsb_atlas::i32(b+4)/1e6;out.lon=adsb_atlas::i32(b+8)/1e6;out.population=adsb_atlas::u32(b+12);
        if(fabs(out.lat)>90||fabs(out.lon)>180)return false;
        memcpy(out.name,b+16,48);memcpy(out.country,b+64,40);memcpy(out.region,b+104,12);return true;
    }
public:
    void close(){index.close();data.close();names=records=0;}
    bool ready()const{return index.p && data.p && names;}
    bool open(const char *root) {
        close();if(!index.open(std::string(root)+"/cities-v1.idx")||!data.open(std::string(root)+"/cities-v1.dat")){close();return false;}
        uint8_t h[32],d[16];long a=adsb_atlas::file_size(index.p),b=adsb_atlas::file_size(data.p);
        if(!adsb_atlas::exact(index.p,h,32)||!adsb_atlas::exact(data.p,d,16)||memcmp(h,"ADSBCI1\0",8)||memcmp(d,"ADSBCD1\0",8)||adsb_atlas::u32(h+8)!=1||memcmp(h+24,d+8,8)){close();return false;}
        names=adsb_atlas::u32(h+12);records=adsb_atlas::u32(h+16);
        if(!names||names>2000000||!records||records>1000000||a!=32+long(names)*80||b!=16+long(records)*120){close();return false;}return true;
    }
    Results search(const char *query,const char *country="",uint32_t token=0) {
        Results result{};result.token=token;result.available=ready();auto key=normalized(query);
        if(!result.available || key.size()<2)return result;
        uint8_t e[80];uint32_t lo=0,hi=names;
        while(lo<hi){auto mid=lo+(hi-lo)/2;if(!entry(mid,e)){result.available=false;return result;}if(strcmp((const char*)e,key.c_str())<0)lo=mid+1;else hi=mid;}
        struct Hit{uint32_t row,pop;bool exact;};std::array<Hit,20> best;unsigned count=0,scanned=0;
        auto before=[](const Hit&a,const Hit&b){return a.exact!=b.exact?a.exact:a.pop!=b.pop?a.pop>b.pop:a.row<b.row;};
        for(uint32_t row=lo;row<names;row++) {
            if(!entry(row,e)){result.available=false;result.count=0;return result;}
            if(strncmp((const char*)e,key.c_str(),key.size()))break;
            if(++scanned>8192){result.more=true;break;}
            if(*country && strncmp(country,(const char*)e+72,2))continue;
            Hit h{adsb_atlas::u32(e+64),adsb_atlas::u32(e+68),!strcmp((const char*)e,key.c_str())};bool duplicate=false;
            for(unsigned i=0;i<count;i++)if(best[i].row==h.row){if(h.exact)best[i].exact=true;duplicate=true;}
            if(duplicate)continue;
            if(count<20)best[count++]=h;else {result.more=true;auto worst=std::max_element(best.begin(),best.end(),before);if(before(h,*worst))*worst=h;}
        }
        std::sort(best.begin(),best.begin()+count,before);
        for(unsigned i=0;i<count;i++){if(!city(best[i].row,result.cities[result.count])){result.available=false;result.count=0;return result;}result.count++;}
        return result;
    }
};
// Worker interface: fixed-size copied messages, no LVGL or I/O on the caller.
bool request(const char *query,const char *country,uint32_t token);
bool receive(Results &out);
}
