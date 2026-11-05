#pragma once
#include "../data/services.h"
#include "geography.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cctype>

namespace adsb_ui {
// Canvas views and list views must consume this same ordered, visible list.
enum class View { Map, Radar, Arrivals, Stats };
enum class Modal { None, Detail, Filters, Settings, Search, Menu, Help, Exit, Alerts, Errors, Cities };
enum class Sort { Distance, Callsign, Altitude, Speed };
constexpr unsigned arrivals_ranges[] = {5,20,50,100,0,150};
enum class Key { None, Back, Previous, Next, Filters, Settings, Menu, Up, Down,
                 Left, Right, Enter, Tab, Home, End, Backspace, PageUp, PageDown, Count };
constexpr unsigned ranges[] = {150,100,50,20,5,1};
constexpr const char *view_names[] = {"Map","Radar","Arrivals","Stats"};
constexpr const char *filter_names[] = {"All","COM","MIL","EMG","HELI","FAST","SLOW","ODD"};

// Navigation is edge-triggered. Only motion repeats, at most once per UI tick.
class KeyGate {
    bool held[(unsigned)Key::Count] = {};
    Key repeat = Key::None;
    uint64_t due = 0;
public:
    bool edge(Key key, bool down, uint64_t now) {
        unsigned i=(unsigned)key;
        if (!i || i >= (unsigned)Key::Count) return false;
        if (!down) { held[i]=false; if(repeat==key) repeat=Key::None; return false; }
        if(held[i]) return false;
        held[i]=true;
        if(key==Key::Up || key==Key::Down || key==Key::Left || key==Key::Right ||
           key==Key::PageUp || key==Key::PageDown) { repeat=key; due=now+400; }
        return true;
    }
    Key tick(uint64_t now) {
        if(repeat==Key::None || now<due) return Key::None;
        due=now+90; return repeat;
    }
    void cancel_repeat() { repeat=Key::None; }
};
inline bool contains(const char *value, const char *query) {
    if(!*query) return true;
    for(;*value;value++) {
        const char *a=value, *b=query;
        while(*a && *b && std::tolower((unsigned char)*a)==std::tolower((unsigned char)*b)) {a++;b++;}
        if(!*b) return true;
    }
    return false;
}
inline bool airline(const adsb_aircraft_t &a) {
    return (a.callsign[0]>='A' && a.callsign[0]<='Z' && a.callsign[1]>='A' && a.callsign[1]<='Z' &&
            a.callsign[2]>='A' && a.callsign[2]<='Z' && a.callsign[3]>='0' && a.callsign[3]<='9');
}
inline bool helicopter(const adsb_aircraft_t &a) {
    if(!strcmp(a.category,"A7")) return true;
    constexpr const char *types[]={"R22","R44","R66","EC35","EC45","EC55","A109","A139","A169","B06","B212","B412",
        "S76","S92","B407","B429","B505","H135","H145","H160","H175","H225","AS50","AS55","AS65","MD52","MD60",
        "NH90","CH47","V22","UH1","BK17"};
    for(auto t:types) if(!strcmp(a.type,t)) return true;
    return false;
}
inline bool passes(const adsb_aircraft_t &a, unsigned filter) {
    switch(filter) {
    case 1: return airline(a) || (a.category[0]=='A' && a.category[1]>='3');
    case 2: return a.military;
    case 3: return a.emergency;
    case 4: return helicopter(a);
    case 5: return a.speed>300 && !a.ground;
    case 6: return a.speed>0 && a.speed<100 && !a.ground;
    case 7: return a.category[0]=='B' || (a.registration[0]=='N' && !airline(a) &&
        ((!strcmp(a.category,"A1") || !strcmp(a.category,"A2")) || (!a.category[0] && a.speed<200)));
    default: return true;
    }
}
inline double distance(double lat, double lon, double center_lat, double center_lon) {
    constexpr double r=0.017453292519943295;
    double a=pow(sin((lat-center_lat)*r/2),2)+cos(lat*r)*cos(center_lat*r)*pow(sin((lon-center_lon)*r/2),2);
    return 6880.13*asin(sqrt(std::min(1.,a)));
}
struct Navigation {
    View view=View::Map;
    Modal modal=Modal::None;
    Sort sort=Sort::Distance;
    unsigned filter=0, visible[200]={}, count=0, page=0, detail_token=0;
    char selected[7]={}, detail[7]={}, tracking[7]={}, query[80]={};
    bool selection_lost=false, centered=true, descending=false;
    double center_lat=0, center_lon=0;
    uint64_t last_action=0, last_cycle=0;
    adsb_settings_t settings={};

    unsigned zoom() const { return view==View::Radar ? settings.preferences.radar_zoom : settings.preferences.map_zoom; }
    unsigned range() const { return ranges[std::min(zoom(),5u)]; }
    void activity(uint64_t now) { last_action=now; last_cycle=now; }
    void close() { if(modal==Modal::Detail) {detail[0]=0; ++detail_token;} modal=Modal::None; }
    void open(Modal m) { close(); modal=m; }
    void switch_view(int delta) { close(); view=(View)(((int)view+delta+4)%4); selected[0]=0; selection_lost=false; page=0; }
    void direct_view(unsigned n) { switch_view((int)n-(int)view); }
    void recenter() { centered=true; tracking[0]=0; center_lat=settings.lat; center_lon=settings.lon; }
    void step_range(int delta) {
        if(view!=View::Map && view!=View::Radar) return;
        auto &z=view==View::Map ? settings.preferences.map_zoom : settings.preferences.radar_zoom;
        z=(unsigned)std::max(0,std::min(5,(int)z+delta));
    }
    const adsb_aircraft_t *find(const adsb_snapshot_t &s,const char *hex) const {
        for(unsigned i=0;i<s.count;i++) if(!strcmp(s.aircraft[i].hex,hex)) return &s.aircraft[i];
        return nullptr;
    }
    void rebuild(const adsb_snapshot_t &s) {
        if(centered) { center_lat=settings.lat; center_lon=settings.lon; }
        if(*tracking) {
            auto a=find(s,tracking);
            if(a) {center_lat=a->lat;center_lon=a->lon;} else recenter();
        }
        count=0;double home_distance[200]={};
        // A home update invalidates the previous projection immediately, even before the worker publishes.
        bool same_home=s.home.lat==settings.lat && s.home.lon==settings.lon && s.home.radius_nm==settings.radius_nm && s.home.feed_provider==settings.feed_provider;
        adsb_geo::Projection projection(center_lat,center_lon,range(),view==View::Radar?199:400);
        if(same_home) for(unsigned i=0;i<s.count && i<200;i++) {
            auto &a=s.aircraft[i];
            if(!passes(a,filter) || !(contains(a.callsign,query)||contains(a.registration,query)||contains(a.hex,query))) continue;
            if(sort==Sort::Distance || view==View::Arrivals)home_distance[i]=distance(a.lat,a.lon,settings.lat,settings.lon);
            if(view==View::Map || view==View::Radar) {
                double nm=view==View::Radar ? (center_lat==settings.lat && center_lon==settings.lon && sort==Sort::Distance ? home_distance[i] : distance(a.lat,a.lon,center_lat,center_lon)) : 0;
                auto p=projection(a.lat,a.lon);
                if(view==View::Radar ? nm>range() : (p.x<0 || p.x>=800 || p.y<0 || p.y>=418))continue;
            }
            if(view==View::Arrivals) {
                unsigned limit=arrivals_ranges[std::min(settings.preferences.arrivals_filter,5u)];
                if(limit && home_distance[i]>limit) continue;
            }
            if(view!=View::Stats) visible[count++]=i;
        }
        std::sort(visible,visible+count,[&](unsigned x,unsigned y) {
            auto &a=s.aircraft[x]; auto &b=s.aircraft[y]; int cmp=0;
            if(sort==Sort::Callsign) cmp=strcmp(a.callsign,b.callsign);
            else if(sort==Sort::Altitude) cmp=(a.altitude>b.altitude)-(a.altitude<b.altitude);
            else if(sort==Sort::Speed) cmp=(a.speed>b.speed)-(a.speed<b.speed);
            else {double da=home_distance[x], db=home_distance[y]; cmp=(da>db)-(da<db);}
            return cmp ? (descending ? cmp>0:cmp<0) : strcmp(a.hex,b.hex)<0;
        });
        if(*selected) {
            bool found=false;
            for(unsigned i=0;i<count;i++) if(!strcmp(s.aircraft[visible[i]].hex,selected)) found=true;
            if(!found) {selected[0]=0; selection_lost=true;}
        }
    }
    int selected_row(const adsb_snapshot_t &s) const {
        for(unsigned i=0;i<count;i++) if(!strcmp(s.aircraft[visible[i]].hex,selected)) return (int)i;
        return -1;
    }
    void move(const adsb_snapshot_t &s,int delta) {
        if(!count) return;
        int i=selected_row(s);
        i=i<0 ? (delta<0 ? (int)count-1:0) : ((i+delta)%(int)count+(int)count)%(int)count;
        snprintf(selected,sizeof(selected),"%s",s.aircraft[visible[i]].hex); selection_lost=false;
    }
    bool open_detail(const adsb_snapshot_t &s) {
        if(selected_row(s)<0) return false;
        open(Modal::Detail); snprintf(detail,sizeof(detail),"%s",selected); ++detail_token; return true;
    }
    bool accepts(const adsb_enrichment_t &e) const {return modal==Modal::Detail && e.token==detail_token && !strcmp(e.hex,detail);}
    bool tick(uint64_t now) {
        if(!settings.preferences.cycle || modal!=Modal::None || *selected || selection_lost ||
           now-last_action<(uint64_t)settings.preferences.cycle_idle_seconds*1000) {last_cycle=now;return false;}
        unsigned seconds=view==View::Stats ? 5:settings.preferences.cycle_seconds;
        if(now-last_cycle<(uint64_t)seconds*1000) return false;
        switch_view(1); last_cycle=now;
        if(view==View::Map) {settings.preferences.map_zoom=(settings.preferences.map_zoom+1)%6;settings.preferences.radar_zoom=settings.preferences.map_zoom;}
        return true;
    }
};
} // namespace adsb_ui
