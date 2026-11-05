#include "maps.h"
#include "atlas.h"
#include "cities.h"
#include "esp_heap_caps.h"
#include "sd_storage.h"
extern "C" {
#include "bsp/input.h"
}
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "atlas_diagnostic.h"
#include "nvs.h"
#include <atomic>

namespace adsb_maps {
namespace {
constexpr const char *root="/sd/apps/com.github.siviqt.adsb";
struct Job {Request request;uint32_t generation;};
struct Result {Job job;uint8_t *pixels;bool ok;};
struct CityJob {char query[80],country[4];uint32_t token;};
std::atomic<bool> radio_ready{false},sd_present{false},sd_known{false},media_changed{false};
std::atomic<uint32_t> generation{0};
std::atomic<unsigned> state{0}; // 0 loading, 1 ready, 2 no SD, 3 missing/invalid atlas, 4 mount error
std::atomic<bool> diagnostic{false},diagnostic_done{false};
std::atomic<bool> stopping{false},stopped{false};
QueueHandle_t requests=nullptr,results=nullptr,city_requests=nullptr,city_results=nullptr;
Result active={};lv_image_dsc_t descriptor={};Request wanted={};bool have_wanted=false;uint64_t retry=0;
SdStorage storage;
bool mount_sd() {
    if(!sd_known.load()) {
        bool present=false;
        if(bsp_input_read_action(BSP_INPUT_ACTION_TYPE_SD_CARD,&present)!=ESP_OK)return false;
        sd_present.store(present);sd_known.store(true);
    }
    return storage.mount(radio_ready.load(),sd_present.load());
}
bool cancelled(void *context){return static_cast<Job*>(context)->generation!=generation.load() || !sd_present.load();}
void worker(void*) {
    adsb_atlas::World world;adsb_cities::Database cities;uint64_t probe_due=0;bool diagnosed=false;
    for(;;) {
        if(stopping.load()) {
            world.close();cities.close();
            bool released=storage.release(radio_ready.load());
            ESP_LOGI("adsb_atlas","SD shutdown: %s",released?"released":"unmount failed");
            stopped.store(released);
            // Only this worker owns storage. Never resume reads after shutdown.
            for(;;)vTaskDelay(portMAX_DELAY);
        }
        if(media_changed.exchange(false)) {
            world.close();cities.close();state.store(0);probe_due=0;
            storage.reset();
        }
        // Automatic discovery also runs when Map is hidden or Wi-Fi is offline.
        bool mounted=mount_sd();
        // A card event can arrive while the BSP is mounting. Process it and close
        // readers before publishing data from this mount or touching the filesystem.
        if(media_changed.load())continue;
        if(!sd_present.load())state.store(2);
        else if(!mounted && storage.attempted())state.store(4);
        if(mounted && (uint64_t)esp_timer_get_time()>=probe_due) {
            probe_due=esp_timer_get_time()+5000000;
            try {
                if(!world.ready())state.store(world.open(root)?1:3);
                if(!cities.ready())cities.open(root);
            } catch(const std::bad_alloc&) {world.close();cities.close();state.store(3);}
        }
        if(diagnostic.load() && !diagnosed && world.ready() && cities.ready()) {
            diagnosed=true;
            try {adsb_atlas::diagnose(world,cities,root);}catch(const std::bad_alloc&){ESP_LOGE("adsb_atlas","Diagnostic allocation failed");}
            nvs_handle_t nvs;if(nvs_open("siviqt_adsb",NVS_READWRITE,&nvs)==ESP_OK){nvs_set_u8(nvs,"atlas_test",0);nvs_commit(nvs);nvs_close(nvs);}
            diagnostic_done.store(true);
        }
        CityJob city;
        if(xQueueReceive(city_requests,&city,0)==pdTRUE) {
            adsb_cities::Results found{};found.token=city.token;
            try {if(mounted)found=cities.search(city.query,city.country,city.token);}
            catch(const std::bad_alloc&){found.available=false;}
            ESP_LOGI("adsb_atlas","City search '%s': %u results, available=%d",city.query,found.count,found.available);
            xQueueOverwrite(city_results,&found);
        }
        Job job;
        if(xQueueReceive(requests,&job,pdMS_TO_TICKS(100))!=pdTRUE)continue;
        Result result{job,nullptr,false};
        if(mounted && world.ready()) {
            result.pixels=(uint8_t*)heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM);
            int64_t started=esp_timer_get_time();
            try {if(result.pixels)result.ok=world.render(job.request,(uint16_t*)result.pixels,cancelled,&job);}
            catch(const std::bad_alloc&){result.ok=false;}
            if(!result.ok){free(result.pixels);result.pixels=nullptr;if(!cancelled(&job))state.store(3);}
            else ESP_LOGI("adsb_atlas","Rendered %.5f,%.5f / %u nm in %lld ms; PSRAM %u KiB",job.request.lat,job.request.lon,job.request.range,(long long)((esp_timer_get_time()-started)/1000),(unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)/1024));
        }
        Result previous;
        // Latest result wins, but every superseded pixel allocation is freed.
        if(xQueueReceive(results,&previous,0)==pdTRUE)free(previous.pixels);
        xQueueOverwrite(results,&result);
    }
}
}
void radio_initialized(){radio_ready.store(true);}
void network_ready(bool){} // Kept for the existing platform hook; association does not gate SD.
void sd_event(bool present){sd_present.store(present);sd_known.store(true);media_changed.store(true);have_wanted=false;generation.fetch_add(1);}
void request_sd(){media_changed.store(true);have_wanted=false;generation.fetch_add(1);}
bool stop() {
    if(!requests)return true;
    stopping.store(true);generation.fetch_add(1);
    for(unsigned n=0;n<500 && !stopped.load();n++)vTaskDelay(pdMS_TO_TICKS(10));
    return stopped.load();
}
void start() {
    if(requests)return;
    nvs_handle_t nvs;uint8_t test=0;
    if(nvs_open("siviqt_adsb",NVS_READONLY,&nvs)==ESP_OK){nvs_get_u8(nvs,"atlas_test",&test);nvs_close(nvs);}
    diagnostic.store(test==1);
    requests=xQueueCreate(1,sizeof(Job));results=xQueueCreate(1,sizeof(Result));
    city_requests=xQueueCreate(1,sizeof(CityJob));city_results=xQueueCreate(1,sizeof(adsb_cities::Results));
    // Keep atlas math and SD work off the display core, including between frames.
    if(!requests || !results || !city_requests || !city_results || xTaskCreatePinnedToCore(worker,"adsb_atlas",16384,nullptr,2,nullptr,0)!=pdPASS) {
        state.store(3);
        for(auto queue:{requests,results,city_requests,city_results})if(queue)vQueueDelete(queue);
        requests=results=city_requests=city_results=nullptr;
    }
}
const lv_image_dsc_t *image(Request request,bool enabled,Request *display,bool tracking) {
    if(!requests)return nullptr;
    Result result;
    while(xQueueReceive(results,&result,0)==pdTRUE) {
        if(result.job.generation!=generation.load()){free(result.pixels);continue;}
        lv_image_cache_drop(&descriptor);free(active.pixels);active=result;
        descriptor={};descriptor.header.magic=LV_IMAGE_HEADER_MAGIC;descriptor.header.cf=LV_COLOR_FORMAT_RGB565;
        descriptor.header.w=width;descriptor.header.h=height;descriptor.header.stride=width*2;
        descriptor.data_size=bytes;descriptor.data=active.pixels;
    }
    if(!enabled)return nullptr;
    uint64_t now=xTaskGetTickCount()*portTICK_PERIOD_MS;
    if(!have_wanted || !same(wanted,request) || (!active.pixels && now>=retry)) {
        wanted=request;have_wanted=true;retry=now+5000;
        Job job{request,generation.fetch_add(1)+1};xQueueOverwrite(requests,&job);
    }
    if(!sd_present.load() || !active.pixels)return nullptr;
    if(!same(active.job.request,request) && !(tracking && active.job.request.range==request.range))return nullptr;
    if(display)*display=active.job.request;
    return &descriptor;
}
const char *attribution(){return "Made with Natural Earth";}
const char *status(){switch(state.load()){case 0:return "SD atlas: loading";case 1:return "World map: SD | Natural Earth";case 2:return "World map: insert SD card";case 4:return "SD mount failed; F6 Retry SD assets";default:return "SD atlas missing or invalid; F6 Retry SD assets";}}
}
namespace adsb_cities {
bool request(const char *query,const char *country,uint32_t token) {
    if(!adsb_maps::city_requests)return false;
    adsb_maps::CityJob job{};snprintf(job.query,sizeof(job.query),"%s",query);snprintf(job.country,sizeof(job.country),"%s",country);job.token=token;
    return xQueueOverwrite(adsb_maps::city_requests,&job)==pdTRUE;
}
bool receive(Results &out){return adsb_maps::city_results && xQueueReceive(adsb_maps::city_results,&out,0)==pdTRUE;}
}
extern "C" void adsb_maps_start(){adsb_maps::start();}
extern "C" bool adsb_maps_stop(){return adsb_maps::stop();}
extern "C" void adsb_maps_radio_initialized(){adsb_maps::radio_initialized();}
extern "C" void adsb_maps_network_ready(bool connected){adsb_maps::network_ready(connected);}
extern "C" void adsb_maps_sd_event(bool present){adsb_maps::sd_event(present);}
extern "C" bool adsb_maps_diagnostic_mode(){return adsb_maps::diagnostic.load();}
extern "C" bool adsb_maps_diagnostic_finished(){return adsb_maps::diagnostic_done.load();}
