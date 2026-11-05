#include "data/model.h"
#include "data/body.h"
#include "data/service_status.h"
#include "data/feed_pacing.h"
#include "nvs_guard.h"
#include <cassert>
#include <fstream>
#include <sstream>
#include <memory>
int main(int argc,char **argv) {
 assert(argc==2);
 auto old=std::make_unique<adsb_snapshot_t>(); auto next=std::make_unique<adsb_snapshot_t>();
 std::ifstream f(argv[1]); std::stringstream text; text<<f.rdbuf();
 JsonDocument doc; assert(!deserializeJson(doc,text.str()));
 assert(parse_aircraft(doc,*next,*old,1000)); assert(next->count==2);
 assert(!strcmp(next->aircraft[0].hex,"abc123")); assert(next->aircraft[0].emergency);
 assert(!strcmp(next->aircraft[0].callsign,"TEST123")); assert(next->aircraft[1].ground);
 assert(next->aircraft[0].lat==0); // zero coordinates are valid
 for(unsigned i=0;i<65;i++) { *old=*next; assert(parse_aircraft(doc,*next,*old,2000+i)); }
 assert(next->aircraft[0].trail_count==60);
 *old=*next; deserializeJson(doc,"{\"ac\":[]}");
 assert(parse_aircraft(doc,*next,*old,3000)); assert(next->count==2 && next->aircraft[0].stale_ms==3000);
 *old=*next; assert(parse_aircraft(doc,*next,*old,33000)); assert(next->count==0);
 deserializeJson(doc,"{}"); assert(!parse_aircraft(doc,*next,*old,34000));
 assert(deserializeJson(doc,"{\"ac\":["));
 adsb_settings_t home=default_settings(); assert(home.configured && valid_settings(home));
 assert(home.lat==42.6975 && home.lon==23.3242 && !strcmp(home.city_name,"Sofia"));
 home.lat=home.lon=0;
 assert(model_distance_nm(0,0,&home)==0); assert(fabs(model_distance_nm(1,0,&home)-60.04)<0.1);
 JsonDocument config; settings_json(config,home); adsb_settings_t loaded;
 assert(settings_from_json(config,loaded)); assert(loaded.preferences.trail_points==30);
 config["lat"]=91; assert(!settings_from_json(config,loaded));
 config["lat"]=0; config["preferences"]["points"]=61; assert(!settings_from_json(config,loaded));
 home.lat=NAN; assert(!valid_settings(home)); home.lat=90.1; assert(!valid_settings(home));
 assert(!identifier("../bad",0,8)); assert(!hex_identifier("12345x"));
 BodyBounds body{8}; assert(body.append(3)); assert(body.append(5)); assert(!body.append(1));
 assert(body.complete(true,8)); assert(!body.complete(false,8)); assert(!body.complete(true,9));
 // Parser completion can precede draining the cached response body.
 BodyBounds buffered{8}; assert(buffered.append(1));
 assert(!buffered.complete(true,0,false));
 assert(buffered.append(4)); assert(buffered.complete(true,0,true));
 BodyBounds chunked{8}; assert(chunked.append(2)); assert(chunked.append(3));
 assert(chunked.complete(true,0)); assert(!chunked.complete(false,0));
 char url[160]; home=default_settings(); aircraft_url(url,sizeof(url),home);
 assert(strstr(url,"/42.697500/23.324200/150"));
 assert(aircraft_host(url));assert(!aircraft_host("https://opendata.adsb.fi.example/"));
 home.feed_provider=1;aircraft_url(url,sizeof(url),home);
 assert(!strcmp(url,"https://opendata.adsb.fi/api/v3/lat/42.697500/lon/23.324200/dist/150"));
 assert(aircraft_host(url));
 settings_json(config,home);assert(settings_from_json(config,loaded) && loaded.feed_provider==1);
 config.remove("feed");assert(settings_from_json(config,loaded) && loaded.feed_provider==0);
 config["feed"]=2;assert(!settings_from_json(config,loaded));
 config["feed"]="1";assert(!settings_from_json(config,loaded));
 home.feed_provider=0;
 home.lat=42; home.lon=23; home.radius_nm=50;
 aircraft_url(url,sizeof(url),home); assert(strstr(url,"/42.000000/23.000000/50"));
 doc.clear(); auto many=doc["ac"].to<JsonArray>();
 for(unsigned i=0;i<250;i++) { auto a=many.add<JsonObject>(); char hex[7]; snprintf(hex,sizeof(hex),"%06x",i); a["hex"]=hex; a["lat"]=1; a["lon"]=2; }
 old->count=0; assert(parse_aircraft(doc,*next,*old,40000)); assert(next->count==200);
 struct Fail : ArduinoJson::Allocator {
   void *allocate(size_t) override { return nullptr; }
   void deallocate(void *) override {}
   void *reallocate(void *,size_t) override { return nullptr; }
 } fail;
 JsonDocument failed(&fail); assert(deserializeJson(failed,"{\"ac\":[{\"hex\":\"abc123\"}]}")==DeserializationError::NoMemory);
 home=default_settings(); home.lat=42.6975; home.lon=23.3242; home.configured=true;
 settings_json(config,home); assert(settings_from_json(config,loaded));
 assert(fabs(loaded.lat-42.6975)<1e-8 && fabs(loaded.lon-23.3242)<1e-8);
 // v2 migrations preserve saved homes; explicit unconfigured homes use Sofia.
 config["schema"]=2;config["lat"]=51.5;config["lon"]=-.12;config["poll"]=30;
 assert(settings_from_json(config,loaded) && loaded.lat==51.5 && loaded.lon==-.12 && loaded.city_id==0 && !*loaded.city_name);
 config["configured"]=false;assert(settings_from_json(config,loaded) && loaded.lat==42.6975 && loaded.poll_seconds==30);
 home.city_id=727011;settings_json(config,home);assert(settings_from_json(config,loaded) && loaded.city_id==727011 && !strcmp(loaded.city_name,"Sofia"));
 config["city"]=std::string(100,'x');assert(!settings_from_json(config,loaded));
 assert(adsb_nvs_write_in_bounds(0x9000,0x1000,0x9000,0x4000));
 assert(adsb_nvs_write_in_bounds(0xc000,0x1000,0x9000,0x4000));
 assert(!adsb_nvs_write_in_bounds(0x8fff,1,0x9000,0x4000));
 assert(!adsb_nvs_write_in_bounds(0xc000,0x1001,0x9000,0x4000));
 assert(!adsb_nvs_write_in_bounds(0xd000,1,0x9000,0x4000));
 assert(!adsb_nvs_write_in_bounds(SIZE_MAX-1,8,0x9000,0x4000));
 FeedPacing pacing;
 assert(pacing.interval(5)==5 && pacing.next(true,5,1000,0)==6000);
 pacing.throttle(5);assert(pacing.interval(5)==15);
 assert(pacing.next(false,5,2000,62000)==62000);
 assert(pacing.next(true,5,64000,62000)==79000);
 pacing.throttle(5);assert(pacing.interval(5)==30);
 assert(pacing.next(true,5,100000,0)==130000);
 for(unsigned i=0;i<20;i++)pacing.throttle(5);
 assert(pacing.interval(5)==300);assert(pacing.next(true,300,1000,3601000)==3601000);
 ServiceStatus status;
 status.set(0,"Received 62 aircraft");status.set(2,"TLS certificate/handshake failure");
 assert(!strcmp(status.feed(),"Received 62 aircraft"));
 status.set(1,"Optional service cooldown");assert(!strcmp(status.feed(),"Received 62 aircraft"));
 status.set(0,"HTTP throttled (429); waiting before retry");status.set(2,"Photo credit received");
 assert(!strcmp(status.feed(),"HTTP throttled (429); waiting before retry"));
 status.set(0,"Received 53 aircraft");assert(!strcmp(status.feed(),"Received 53 aircraft"));
 char notice[160];status.describe(notice,sizeof(notice),10001,70000,true,true);
 assert(!strcmp(notice,"Feed rate limited; retry in 60s"));
 status.describe(notice,sizeof(notice),69999,70000,true,true);assert(!strcmp(notice,"Feed rate limited; retry in 1s"));
 status.describe(notice,sizeof(notice),70000,70000,true,true);assert(!strcmp(notice,"Aircraft feed retry scheduled"));
 status.set(0,"Waiting for Wi-Fi address and DNS");status.describe(notice,sizeof(notice),20000,70000,true,false);
 assert(!strcmp(notice,"Waiting for Wi-Fi address and DNS"));
 puts("Data replay, adaptive polling/cooldown and independent service status checks passed");
}
