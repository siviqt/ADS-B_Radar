#pragma once
#include "atlas.h"
#include "cities.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace adsb_atlas {
// Explicit, one-shot maintenance test enabled by this app's atlas_test NVS key.
// It never changes the user's location, feed or Wi-Fi profiles.
inline bool diagnose(World &world,adsb_cities::Database &cities,const char *root) {
    File report;report.p=fopen((std::string(root)+"/atlas-device-test.json").c_str(),"wb");
    if(!report.p)return false;
    auto pixels=(uint16_t*)heap_caps_malloc(800*418*2,MALLOC_CAP_SPIRAM);if(!pixels)return false;
    bool passed=true;unsigned cases=0;long long worst=0;
    // Read the stored bytes back independently of BadgeLink's incoming-transfer CRC.
    fprintf(report.p,"{\"offline\":true,\"files\":[");
    File manifest;auto json=(char*)heap_caps_malloc(8192,MALLOC_CAP_SPIRAM);JsonDocument info;
    bool manifest_ok=json && manifest.open(std::string(root)+"/atlas-v2.json");
    size_t length=manifest_ok?fread(json,1,8192,manifest.p):0;
    manifest_ok=manifest_ok && length<8192 && !deserializeJson(info,static_cast<const char*>(json),length);manifest.close();free(json);
    auto buffer=(uint8_t*)heap_caps_malloc(32768,MALLOC_CAP_SPIRAM);if(!buffer){free(pixels);return false;}
    unsigned files=0;
    for(const char *name:{"world-v2.idx","world-v2-000.avp","cities-v1.idx","cities-v1.dat","DATA_NOTICES.txt"}) {
        File file;bool ok=manifest_ok && file.open(std::string(root)+"/"+name);uint32_t crc=0;size_t bytes=0;
        if(ok) {
            size_t n;while((n=fread(buffer,1,32768,file.p))!=0){crc=esp_rom_crc32_le(crc,buffer,n);bytes+=n;vTaskDelay(1);}
            ok=!ferror(file.p) && bytes==info["files"][name]["bytes"].as<size_t>() && crc==info["files"][name]["crc32"].as<uint32_t>();
        }
        passed&=ok;fprintf(report.p,"%s{\"name\":\"%s\",\"bytes\":%u,\"crc32\":%lu,\"ok\":%s}",files++?",":"",name,(unsigned)bytes,(unsigned long)crc,ok?"true":"false");
    }
    free(buffer);
    struct Center {double lat,lon;const char *name;};
    const Center centers[]={{42.6975,23.3242,"Sofia"},{40.7128,-74.006,"New York"},{-33.8688,151.2093,"Sydney"},{35.6762,139.6503,"Tokyo"},{0,-140,"Ocean"},{-17.7134,178.065,"Fiji"},{0,179.999,"Date line"},{89,45,"Arctic"},{90,0,"North Pole"},{-90,0,"South Pole"},{64.8378,-147.7164,"Fairbanks"}};
    fprintf(report.p,"],\"maps\":[");
    for(const auto &c:centers)for(unsigned range:{150u,100u,50u,20u,5u,1u}) {
        auto started=esp_timer_get_time();bool ok=world.render({c.lat,c.lon,range},pixels);long long ms=(esp_timer_get_time()-started)/1000;
        fprintf(report.p,"%s{\"place\":\"%s\",\"range_nm\":%u,\"ok\":%s,\"ms\":%lld}",cases++?",":"",c.name,range,ok?"true":"false",ms);
        passed&=ok;worst=std::max(worst,ms);vTaskDelay(1);
    }
    free(pixels);fprintf(report.p,"],\"cities\":[");unsigned searches=0;
    for(const char *name:{"Sofia","London","Munich","New York","Sydney","Tokyo","Springfield","Lo","San"}) {
        auto started=esp_timer_get_time();auto result=cities.search(name);long long ms=(esp_timer_get_time()-started)/1000;
        bool ok=result.available && result.count;passed&=ok;
        fprintf(report.p,"%s{\"query\":\"%s\",\"count\":%u,\"ok\":%s,\"ms\":%lld}",searches++?",":"",name,result.count,ok?"true":"false",ms);
        vTaskDelay(1);
    }
    auto canada=cities.search("London","CA");auto sofia=cities.search("Sofia","BG");
    passed&=canada.available && canada.count && !strcmp(canada.cities[0].country,"Canada");
    passed&=sofia.available && sofia.count==1 && !strcmp(sofia.cities[0].country,"Bulgaria");
    passed&=cities.search("qzxqzxqzx").count==0;
    fprintf(report.p,"],\"passed\":%s,\"map_max_ms\":%lld}\n",passed?"true":"false",worst);
    if(fflush(report.p)||ferror(report.p))passed=false;
    report.close();
    ESP_LOGI("adsb_atlas","Offline diagnostic: %s; %u map cases, %u city searches, max map %lld ms",passed?"PASS":"FAIL",cases,searches,worst);
    return passed;
}
}
