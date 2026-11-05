#pragma once
#include "services.h"
#include <ArduinoJson.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cctype>
inline adsb_settings_t default_settings() {
    adsb_settings_t s={}; s.radius_nm=150; s.poll_seconds=5; s.lat=42.6975; s.lon=23.3242; s.configured=true;
    snprintf(s.city_name,sizeof(s.city_name),"Sofia");snprintf(s.city_country,sizeof(s.city_country),"Bulgaria");
    s.preferences={false,true,true,true,true,true,false,30,0,15,60,1,0,4}; return s;
}
inline bool valid_settings(const adsb_settings_t &s) {
    return std::isfinite(s.lat) && std::isfinite(s.lon) && s.lat >= -90 && s.lat <= 90 &&
        s.lon >= -180 && s.lon <= 180 && s.radius_nm >= 1 && s.radius_nm <= 250 &&
        s.poll_seconds >= 5 && s.poll_seconds <= 300 && s.feed_provider<=1 &&
        memchr(s.city_name,0,sizeof(s.city_name)) && memchr(s.city_country,0,sizeof(s.city_country)) &&
        s.preferences.trail_points>=10 && s.preferences.trail_points<=60 && s.preferences.trail_style<=1 &&
        s.preferences.cycle_seconds>=15 && s.preferences.cycle_seconds<=120 &&
        s.preferences.cycle_idle_seconds>=15 && s.preferences.cycle_idle_seconds<=600 &&
        s.preferences.map_zoom<=5 && s.preferences.radar_zoom<=5 && s.preferences.arrivals_filter<=5;
}
inline bool identifier(const char *s, size_t min, size_t max) {
    size_t n = strlen(s); if (n < min || n > max) return false;
    for (; *s; ++s) if (!isalnum((unsigned char)*s)) return false;
    return true;
}
inline bool hex_identifier(const char *s) {
    if (strlen(s) != 6) return false;
    for (; *s; ++s) if (!isxdigit((unsigned char)*s)) return false;
    return true;
}
inline double model_distance_nm(double lat, double lon, const adsb_settings_t *h) {
    const double r = 0.017453292519943295;
    double a = pow(sin((lat-h->lat)*r/2),2) + cos(lat*r)*cos(h->lat*r)*pow(sin((lon-h->lon)*r/2),2);
    return 3440.065 * 2 * asin(sqrt(fmin(1., a)));
}
inline const char *feed_name(unsigned provider) {return provider==1?"adsb.fi":"adsb.lol";}
inline const char *feed_site(unsigned provider) {return provider==1?"https://adsb.fi":"https://adsb.lol";}
inline bool aircraft_host(const char *url) {
 return !strncmp(url,"https://api.adsb.lol/",sizeof("https://api.adsb.lol/")-1) || !strncmp(url,"https://opendata.adsb.fi/",sizeof("https://opendata.adsb.fi/")-1);
}
inline void aircraft_url(char *out,size_t size,const adsb_settings_t &home) {
 if(home.feed_provider==1)snprintf(out,size,"https://opendata.adsb.fi/api/v3/lat/%.6f/lon/%.6f/dist/%u",home.lat,home.lon,home.radius_nm);
 else snprintf(out,size,"https://api.adsb.lol/v2/point/%.6f/%.6f/%u",home.lat,home.lon,home.radius_nm);
}
inline bool parse_aircraft(JsonDocument &doc, adsb_snapshot_t &next, const adsb_snapshot_t &old, uint64_t now) {
    if (!doc["ac"].is<JsonArray>()) return false;
    next.count = 0;
    for (JsonObject obj : doc["ac"].as<JsonArray>()) {
        const char *hex = obj["hex"] | "";
        if (!hex_identifier(hex) || !obj["lat"].is<double>() || !obj["lon"].is<double>()) continue;
        double lat = obj["lat"], lon = obj["lon"];
        if (!std::isfinite(lat) || !std::isfinite(lon) || fabs(lat)>90 || fabs(lon)>180) continue;
        char key[7]; snprintf(key, sizeof(key), "%s", hex);
        for (char &c : key) c = tolower((unsigned char)c);
        bool duplicate = false;
        for (unsigned i=0;i<next.count;i++) if (!strcmp(next.aircraft[i].hex,key)) duplicate = true;
        if (duplicate || next.count == 200) continue;
        adsb_aircraft_t &a = next.aircraft[next.count++]; a = {};
        for (unsigned i=0;i<old.count;i++) if (!strcmp(old.aircraft[i].hex,key)) { a=old.aircraft[i]; break; }
        snprintf(a.hex,sizeof(a.hex),"%s",key);
        snprintf(a.callsign,sizeof(a.callsign),"%s",obj["flight"] | "");
        for (int i=(int)strlen(a.callsign)-1;i>=0 && a.callsign[i]==' ';i--) a.callsign[i]=0;
        snprintf(a.registration,sizeof(a.registration),"%s",obj["r"] | "");
        snprintf(a.type,sizeof(a.type),"%s",obj["t"] | "");
        snprintf(a.category,sizeof(a.category),"%s",obj["category"] | "");
        snprintf(a.description,sizeof(a.description),"%s",obj["desc"] | "");
        snprintf(a.operator_name,sizeof(a.operator_name),"%s",obj["ownOp"] | "");
        a.lat=lat; a.lon=lon; a.ground=obj["alt_baro"] == "ground";
        auto integer = [&](const char *k, int lo, int hi) { double v=obj[k] | 0.; return std::isfinite(v) ? (int)fmax(lo,fmin(hi,v)) : 0; };
        a.vertical_rate=integer("baro_rate",-30000,30000); a.ias=integer("ias",0,4000); a.tas=integer("tas",0,4000);
        a.nav_altitude=integer("nav_altitude_mcp",0,100000);
        auto real = [&](const char *k, double lo, double hi) { double v=obj[k] | 0.; return (float)(std::isfinite(v) ? fmax(lo,fmin(hi,v)) : 0.); };
        a.mach=real("mach",0,10); a.roll=real("roll",-180,180); a.nav_qnh=real("nav_qnh",0,1500);
        a.altitude=integer("alt_baro",-2000,100000); a.speed=integer("gs",0,4000); a.heading=integer("track",0,359);
        const char *sq=obj["squawk"] | "";
        a.squawk=identifier(sq,4,4) ? (int)strtoul(sq,nullptr,10) : 0;
        a.emergency=!strcmp(sq,"7500") || !strcmp(sq,"7600") || !strcmp(sq,"7700");
        unsigned code=strtoul(key,nullptr,16);
        a.military=(code>=0xADF7C8 && code<=0xAFFFFF) || (code>=0xA00001 && code<=0xA00FFF) ||
            (code>=0x43C000 && code<=0x43CFFF) || (code>=0x3B0000 && code<=0x3BFFFF) ||
            (code>=0x3F4000 && code<=0x3F7FFF) || (code>=0xC0CDF9 && code<=0xC0FFFF) ||
            (code>=0x7C8000 && code<=0x7CBFFF) || (code>=0x0A4000 && code<=0x0A4FFF);
        a.seen_ms=now; a.stale_ms=0;
        if (a.trail_count==60) { memmove(a.trail,a.trail+1,59*sizeof(a.trail[0])); a.trail_count--; }
        a.trail[a.trail_count++]={(float)lat,(float)lon,a.altitude};
    }
    for (unsigned i=0;i<old.count && next.count<200;i++) {
        auto a=old.aircraft[i]; bool found=false;
        for(unsigned j=0;j<next.count;j++) if(!strcmp(a.hex,next.aircraft[j].hex)) found=true;
        if(found) continue;
        if(!a.stale_ms) a.stale_ms=now;
        if(now-a.stale_ms<30000) next.aircraft[next.count++]=a;
    }
    next.updated_ms=now; return true;
}

inline void settings_json(JsonDocument &d, const adsb_settings_t &s) {
 d["schema"]=3; d["configured"]=s.configured; d["city_id"]=s.city_id; d["city"]=s.city_name; d["country"]=s.city_country; d["lat"]=s.lat; d["lon"]=s.lon; d["radius"]=s.radius_nm; d["poll"]=s.poll_seconds; d["feed"]=s.feed_provider;
 auto p=d["preferences"].to<JsonObject>();
 p["metric"]=s.preferences.metric; p["trails"]=s.preferences.trails; p["cycle"]=s.preferences.cycle;
 p["military"]=s.preferences.alert_military; p["emergency"]=s.preferences.alert_emergency; p["autofocus"]=s.preferences.alert_autofocus;
 p["motion"]=s.preferences.reduce_motion; p["points"]=s.preferences.trail_points; p["style"]=s.preferences.trail_style;
 p["interval"]=s.preferences.cycle_seconds; p["idle"]=s.preferences.cycle_idle_seconds;
 p["map"]=s.preferences.map_zoom; p["radar"]=s.preferences.radar_zoom; p["arrivals"]=s.preferences.arrivals_filter;
}
inline bool settings_from_json(JsonDocument &d, adsb_settings_t &s) {
 if((d["schema"]!=2 && d["schema"]!=3) || !d["lat"].is<double>() || !d["lon"].is<double>()) return false;
 auto v=default_settings(); v.lat=d["lat"]; v.lon=d["lon"]; v.radius_nm=d["radius"] | 150u; v.poll_seconds=d["poll"] | 5u;
 if(!d["feed"].isNull() && !d["feed"].is<unsigned>())return false;
 v.feed_provider=d["feed"] | 0u;
 auto p=d["preferences"];
 v.preferences={p["metric"] | false,p["trails"] | true,p["cycle"] | true,
   p["military"] | true,p["emergency"] | true,p["autofocus"] | true,p["motion"] | false,
   p["points"] | 30u,p["style"] | 0u,p["interval"] | 15u,p["idle"] | 60u,
   p["map"] | 1u,p["radar"] | 0u,p["arrivals"] | 4u};
 if(d["schema"]==3) {
   if(!d["city_id"].is<uint32_t>() || !d["city"].is<const char*>() || !d["country"].is<const char*>())return false;
   const char *city=d["city"],*country=d["country"];
   if(strlen(city)>=sizeof(v.city_name)||strlen(country)>=sizeof(v.city_country))return false;
   v.city_id=d["city_id"];snprintf(v.city_name,sizeof(v.city_name),"%s",city);snprintf(v.city_country,sizeof(v.city_country),"%s",country);
 } else {
   v.city_id=0;v.city_name[0]=v.city_country[0]=0;
   if(fabs(v.lat-42.6975)<1e-5 && fabs(v.lon-23.3242)<1e-5) {
     snprintf(v.city_name,sizeof(v.city_name),"Sofia");snprintf(v.city_country,sizeof(v.city_country),"Bulgaria");
   }
 }
 if(d["configured"].is<bool>() && !d["configured"].as<bool>()) {
   auto fallback=default_settings();v.lat=fallback.lat;v.lon=fallback.lon;v.city_id=0;
   memcpy(v.city_name,fallback.city_name,sizeof(v.city_name));memcpy(v.city_country,fallback.city_country,sizeof(v.city_country));
 }
 if(!valid_settings(v)) return false;
 v.configured=true; s=v; return true;
}
