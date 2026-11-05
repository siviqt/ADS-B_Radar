#include "model.h"
#include "body.h"
#include "service_status.h"
#include "feed_pacing.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
extern "C" {
#include "wifi_connection.h"
}
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "nvs.h"
#include <ctime>
#include <cerrno>
#include <atomic>

namespace {
SemaphoreHandle_t mutex;
QueueHandle_t requests, results;
std::atomic<bool> running{false};
adsb_settings_t settings=default_settings();
adsb_snapshot_t *current, *staging;
ServiceStatus messages;
uint64_t generation=0;
adsb_diagnostics_t diagnostics={};
struct Alloc : ArduinoJson::Allocator {
 void *allocate(size_t n) override { return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT); }
 void deallocate(void *p) override { heap_caps_free(p); }
 void *reallocate(void *p,size_t n) override { return heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT); }
} allocator;
uint64_t now_ms() { return esp_timer_get_time()/1000; }
void status(const char *s,unsigned host=0) {
 xSemaphoreTake(mutex,portMAX_DELAY);bool changed=messages.set(host,s);xSemaphoreGive(mutex);
 if(changed)ESP_LOGI("adsb_data","%s: %s",host==0?"Aircraft":host==1?"Route/metadata":"Photo credit",s);
}
uint64_t service_cooldown[3]={};
unsigned host_index(const char *url) { return aircraft_host(url) ? 0 : strstr(url,"api.adsbdb.com/") ? 1 : 2; }
bool online() {
 wifi_ap_record_t ap;esp_netif_ip_info_t ip;
 auto net=wifi_get_netif(); // Null until the manager has initialized the TCP/IP stack.
 if(!net || esp_wifi_sta_get_ap_info(&ap)!=ESP_OK || esp_netif_get_ip_info(net,&ip)!=ESP_OK || !ip.ip.addr)return false;
 for(unsigned i=0;i<ESP_NETIF_DNS_MAX;i++) {
   esp_netif_dns_info_t dns={};
   if(esp_netif_get_dns_info(net,(esp_netif_dns_type_t)i,&dns)!=ESP_OK)continue;
   if(dns.ip.type==ESP_IPADDR_TYPE_V4 ? dns.ip.u_addr.ip4.addr!=0 :
      (dns.ip.u_addr.ip6.addr[0]|dns.ip.u_addr.ip6.addr[1]|dns.ip.u_addr.ip6.addr[2]|dns.ip.u_addr.ip6.addr[3])!=0)return true;
 }
 return false;
}
// One worker owns every HTTPS operation. No second HTTP mutex or UI callback.
struct ResponseHeaders { unsigned retry_seconds=60; };
esp_err_t response_event(esp_http_client_event_t *event) {
 if(event->event_id==HTTP_EVENT_ON_HEADER && event->header_key && event->header_value &&
    !strcasecmp(event->header_key,"Retry-After")) {
   char *end=nullptr; unsigned long seconds=strtoul(event->header_value,&end,10);
   if(end!=event->header_value && !*end)
     static_cast<ResponseHeaders *>(event->user_data)->retry_seconds=(unsigned)fmax(60.,fmin(3600.,(double)seconds));
 }
 return ESP_OK;
}
void record_error(const char *text) {
 xSemaphoreTake(mutex,portMAX_DELAY);
 if(diagnostics.error_count==16) {memmove(diagnostics.errors,diagnostics.errors+1,15*sizeof(adsb_error_t));diagnostics.error_count--;}
 auto &e=diagnostics.errors[diagnostics.error_count++];e.at_ms=now_ms();snprintf(e.text,sizeof(e.text),"%s",text);
 xSemaphoreGive(mutex);
}
bool fetch(const char *url, JsonDocument &doc, size_t limit,int *http_result=nullptr) {
 if(http_result)*http_result=0;
 unsigned host=host_index(url);
 if(now_ms()<service_cooldown[host]) { status(host==0?"Aircraft service cooldown; retaining last aircraft":"Optional service cooldown",host); return false; }
 ESP_LOGI("adsb_data","GET %s",url);
 uint64_t started=now_ms();
 auto fail_early=[&](const char *error) {
   status(error,host);char failure[160];snprintf(failure,sizeof(failure),"%s: %s",host==0?"Aircraft":host==1?"Route/metadata":"Photo credit",error);record_error(failure);xSemaphoreTake(mutex,portMAX_DELAY);
   if(host==0)diagnostics.fetch_fail++;else diagnostics.enrich_fail++;
   xSemaphoreGive(mutex);return false;
 };
 if(!running || !online()) return fail_early("Wi-Fi disconnected; retaining last data");
 if(time(nullptr)<1767225600) return fail_early("Waiting for network time before TLS");
 ResponseHeaders headers;
 esp_http_client_config_t cfg={}; cfg.event_handler=response_event; cfg.user_data=&headers; cfg.url=url; cfg.crt_bundle_attach=esp_crt_bundle_attach;
 cfg.timeout_ms=4000; cfg.buffer_size=2048; cfg.disable_auto_redirect=true;
 auto client=esp_http_client_init(&cfg);
 if(!client) return fail_early("HTTP allocation failed");
 char *body=(char*)allocator.allocate(limit+1);
 if(!body) { esp_http_client_cleanup(client); return fail_early("Body allocation failed"); }
 size_t received=0;
 int tls_code=0,tls_flags=0,http_status=0;
 bool ok=false; const char *error="DNS/TLS/connect failure";
 uint64_t deadline=now_ms()+20000;
 if(esp_http_client_open(client,0)==ESP_OK) {
   int64_t length=esp_http_client_fetch_headers(client);
   int code=esp_http_client_get_status_code(client);http_status=code;if(http_result)*http_result=code;
   ESP_LOGI("adsb_data","HTTP %d, declared bytes %lld",code,(long long)length);
   if(code!=200) {
     error=code==429 ? "HTTP throttled (429); waiting before retry" : "HTTP server/status failure";
     if(code==429 || code==503) {
       service_cooldown[host]=now_ms()+(uint64_t)headers.retry_seconds*1000;
     }
   }
   else if(length<0) error="HTTP headers failed";
   else if((uint64_t)length>limit) error="HTTP body exceeds limit";
   else {
     BodyBounds bounds{limit}; error="Incomplete HTTP body/deadline";
     while(running && now_ms()<deadline) {
       // Read until EOF, including bytes IDF buffered while parsing headers.
       // Its completion flag describes parser progress, not buffered bytes drained.
       // One-byte reads return control for the deadline between body bytes.
       // TLS buffers records; this also avoids IDF's internal fill-length loop.
       int n=esp_http_client_read(client,body+bounds.used,1);
       if(n<0) break;
       received+=(size_t)n;
       if(!bounds.append((size_t)n)) { error="HTTP body exceeds limit"; break; }
       if(n==0) {
         if(!bounds.complete(esp_http_client_is_complete_data_received(client),length)) break;
         // Copy strings because the receive buffer is freed below.
         auto parse=deserializeJson(doc,static_cast<const char *>(body),bounds.used,DeserializationOption::NestingLimit(16));
         ok=!parse; if(!ok) error="Invalid/truncated JSON"; break;
       }
     }
   }
 }
 if(!ok && !strcmp(error,"DNS/TLS/connect failure")) {
   esp_http_client_get_and_clear_last_tls_error(client,&tls_code,&tls_flags);
   if(tls_code || tls_flags) {error="TLS certificate/handshake failure";if(host!=0)service_cooldown[host]=now_ms()+60000;}
   else if(esp_http_client_get_errno(client)==EHOSTUNREACH) error="DNS/host unreachable";
   else error="DNS/socket connection failure";
 }
 if(!ok)ESP_LOGW("adsb_data","Request diagnostics: socket errno=%d TLS code=0x%x flags=0x%x internal free=%u largest=%u",
   esp_http_client_get_errno(client),tls_code,tls_flags,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
   (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
 esp_http_client_close(client); esp_http_client_cleanup(client); allocator.deallocate(body);
 ESP_LOGI("adsb_data","Request %s after %llu ms: %s",url,(unsigned long long)(now_ms()-started),ok ? "OK" : error);
 xSemaphoreTake(mutex,portMAX_DELAY);
 diagnostics.bytes_received+=received;
 if(host==0) {diagnostics.last_fetch_ms=now_ms()-started;if(ok)diagnostics.fetch_ok++;else diagnostics.fetch_fail++;}
 else {if(ok)diagnostics.enrich_ok++;else diagnostics.enrich_fail++;}
 xSemaphoreGive(mutex);
 if(!ok) {
   status(error,host);char failure[160];
   const char *source=host==0?"Aircraft":host==1?"Route/metadata":"Photo credit";
   if(tls_code || tls_flags)snprintf(failure,sizeof(failure),"%s: %s (TLS %x, flags %x)",source,error,(unsigned)tls_code,(unsigned)tls_flags);
   else if(http_status)snprintf(failure,sizeof(failure),"%s: %s (HTTP %d)",source,error,http_status);
   else snprintf(failure,sizeof(failure),"%s: %s",source,error);
   record_error(failure);
 }
 return ok;
}
void enrich(adsb_enrichment_t &e, unsigned stage) {
 char url[160]; JsonDocument doc(&allocator); bool any=false;
 if(stage==0 && identifier(e.callsign,1,8)) {
   snprintf(url,sizeof(url),"https://api.adsbdb.com/v0/callsign/%s",e.callsign);
   if(fetch(url,doc,32768) && doc["response"]["flightroute"].is<JsonObject>()) {
     snprintf(e.origin,sizeof(e.origin),"%s",doc["response"]["flightroute"]["origin"]["iata_code"] | "");
     snprintf(e.destination,sizeof(e.destination),"%s",doc["response"]["flightroute"]["destination"]["iata_code"] | ""); any=true;
   }
 }
 doc.clear(); snprintf(url,sizeof(url),"https://api.adsbdb.com/v0/aircraft/%s",e.hex);
 if(stage==1 && fetch(url,doc,32768) && doc["response"]["aircraft"].is<JsonObject>()) {
   auto a=doc["response"]["aircraft"];
   snprintf(e.registration,sizeof(e.registration),"%s",a["registration"] | "");
   snprintf(e.type,sizeof(e.type),"%s",a["type"] | "");
   snprintf(e.operator_name,sizeof(e.operator_name),"%s",a["registered_owner"] | ""); any=true;
 }
 doc.clear(); snprintf(url,sizeof(url),"https://api.planespotters.net/pub/photos/hex/%s",e.hex);
 if(stage==2 && fetch(url,doc,32768) && doc["photos"][0]["photographer"].is<const char *>()) { snprintf(e.credit,sizeof(e.credit),"%s",doc["photos"][0]["photographer"] | ""); any=true; }
 e.valid=e.valid || any;
}
void worker(void *) {
 while(running && !online()) {status("Waiting for Wi-Fi address and DNS");vTaskDelay(pdMS_TO_TICKS(200));}
 if(!running){vTaskDelete(nullptr);return;}
 esp_sntp_config_t ntp=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
 esp_err_t ntp_err=esp_netif_sntp_init(&ntp);
 if(ntp_err!=ESP_OK) status("Network time initialization failed");
 uint64_t due=0,network_check_due=0;bool network_ready=false;FeedPacing pacing;unsigned last_provider=UINT_MAX;
 adsb_enrichment_t pending={}; unsigned stage=0; bool pending_active=false;
 struct Cache { adsb_enrichment_t entry; uint64_t expires; bool complete; } cache[20]={}; unsigned slot=0;
 while(running) {
   adsb_settings_t home; adsb_settings_get(&home);
   uint64_t version;
   xSemaphoreTake(mutex,portMAX_DELAY); version=generation; xSemaphoreGive(mutex);
   static uint64_t last_version=UINT64_MAX;
   if(version!=last_version) {
     if(home.feed_provider!=last_provider) {last_provider=home.feed_provider;service_cooldown[0]=0;pacing=FeedPacing{};}
     due=std::max(now_ms(),service_cooldown[0]); last_version=version;
     xSemaphoreTake(mutex,portMAX_DELAY); memset(current,0,sizeof(*current)); current->home=home; xSemaphoreGive(mutex);
   }
   if(!home.configured) { status("Enter home latitude,longitude then Enter"); vTaskDelay(pdMS_TO_TICKS(500)); continue; }
   xSemaphoreTake(mutex,portMAX_DELAY);diagnostics.effective_poll_seconds=pacing.interval(home.poll_seconds);xSemaphoreGive(mutex);
   if(now_ms()>=network_check_due) {
     network_check_due=now_ms()+1000;bool previous=network_ready;network_ready=online();
     xSemaphoreTake(mutex,portMAX_DELAY);diagnostics.network_ready=network_ready;xSemaphoreGive(mutex);
     if(network_ready && !previous) {due=std::max(now_ms(),service_cooldown[0]);status("Network ready; waiting for aircraft feed");}
   }
   if(!network_ready) {status("Waiting for Wi-Fi address and DNS; retaining last data");vTaskDelay(pdMS_TO_TICKS(200));continue;}
   if(now_ms()>=due) {
     xSemaphoreTake(mutex,portMAX_DELAY);bool retry=diagnostics.rate_limited;diagnostics.rate_limited=false;xSemaphoreGive(mutex);
     if(retry)status("Retrying aircraft feed after server cooldown");
     char url[160]; aircraft_url(url,sizeof(url),home);
     JsonDocument doc(&allocator);
     int code=0;bool ok=fetch(url,doc,512*1024,&code);
     if(ok) {
       staging->home=home;
       ok=parse_aircraft(doc,*staging,*current,now_ms());
       if(!ok) {status("Invalid aircraft response; retaining last data");record_error("Invalid aircraft response");}
       else {
         xSemaphoreTake(mutex,portMAX_DELAY);
         if(version==generation) { auto tmp=current; current=staging; staging=tmp; }
         xSemaphoreGive(mutex);
         char text[100];
         // current is only exchanged here, so count is safe after release.
         snprintf(text,sizeof(text),"Received %u aircraft; coverage %u nm",current->count,home.radius_nm); status(text);
       }
     }
     if(code==429)pacing.throttle(home.poll_seconds);
     due=pacing.next(ok,home.poll_seconds,now_ms(),service_cooldown[0]);
     xSemaphoreTake(mutex,portMAX_DELAY);diagnostics.effective_poll_seconds=pacing.interval(home.poll_seconds);
     diagnostics.retry_at_ms=due;diagnostics.rate_limited=code==429;xSemaphoreGive(mutex);
   } else {
     if(!pending_active && xQueueReceive(requests,&pending,0)) {
       Cache *hit=nullptr;
       for(auto &c:cache) if(c.expires>now_ms() && !strcmp(c.entry.hex,pending.hex) && !strcmp(c.entry.callsign,pending.callsign)) {
         hit=&c;if(c.complete)break;
       }
       unsigned token=pending.token;
       if(hit) {pending=hit->entry;pending.token=token;}
       if(hit && (hit->complete || token==ADSB_BACKGROUND_ROUTE_TOKEN))xQueueOverwrite(results,&pending);
       else {pending_active=true;stage=hit?1:0;}
     }
     if(pending_active) {
       enrich(pending,stage++);
       if(stage==3 || pending.token==ADSB_BACKGROUND_ROUTE_TOKEN) {
         cache[slot++%20]={pending,now_ms()+(pending.valid ? 3600000 : 60000),pending.token!=ADSB_BACKGROUND_ROUTE_TOKEN};
         xQueueOverwrite(results,&pending); pending_active=false;
       }
     }
   }
   vTaskDelay(pdMS_TO_TICKS(100));
 }
 if(ntp_err==ESP_OK) esp_netif_sntp_deinit();
 vTaskDelete(nullptr);
}
}
bool adsb_settings_get(adsb_settings_t *out) {
 if(!mutex || !out) return false;
 xSemaphoreTake(mutex,portMAX_DELAY); *out=settings; xSemaphoreGive(mutex); return true;
}
bool adsb_settings_save(const adsb_settings_t *s) {
 if(!mutex || !s || !valid_settings(*s) || !s->configured) return false;
 // Store one versioned record atomically in this app's namespace only.
 JsonDocument doc(&allocator); settings_json(doc,*s);
 char record[1024]; if(doc.overflowed() || measureJson(doc)>=sizeof(record)) return false;
 serializeJson(doc,record,sizeof(record));
 nvs_handle_t h; if(nvs_open("siviqt_adsb",NVS_READWRITE,&h)!=ESP_OK) return false;
 esp_err_t err=nvs_set_str(h,"settings",record); if(err==ESP_OK) err=nvs_commit(h); nvs_close(h);
 if(err!=ESP_OK) return false;
 xSemaphoreTake(mutex,portMAX_DELAY); bool geometry_changed=settings.lat!=s->lat || settings.lon!=s->lon || settings.radius_nm!=s->radius_nm || settings.configured!=s->configured || settings.feed_provider!=s->feed_provider;
 settings=*s; if(geometry_changed) generation++; xSemaphoreGive(mutex); return true;
}
bool adsb_snapshot_copy(adsb_snapshot_t *out) {
 if(!mutex || !out || !current) return false;
 xSemaphoreTake(mutex,portMAX_DELAY); *out=*current; xSemaphoreGive(mutex); return true;
}
void adsb_service_status(char *out,size_t size) {
 if(!mutex) { snprintf(out,size,"Data service unavailable"); return; }
 xSemaphoreTake(mutex,portMAX_DELAY);
 messages.describe(out,size,now_ms(),diagnostics.retry_at_ms,diagnostics.rate_limited,diagnostics.network_ready);
 xSemaphoreGive(mutex);
}
bool adsb_enrich_request(const char *hex,const char *callsign,unsigned token) {
 if(!running || !hex || !callsign || !hex_identifier(hex) || !identifier(callsign,0,8)) return false;
 adsb_enrichment_t e={}; snprintf(e.hex,sizeof(e.hex),"%s",hex); snprintf(e.callsign,sizeof(e.callsign),"%s",callsign); e.token=token;
 return xQueueSend(requests,&e,0)==pdTRUE;
}
bool adsb_enrich_receive(adsb_enrichment_t *out) { return results && out && xQueueReceive(results,out,0)==pdTRUE; }
void adsb_services_stop(void) { running=false; }
bool adsb_services_start(void) {
 mutex=xSemaphoreCreateMutex(); requests=xQueueCreate(2,sizeof(adsb_enrichment_t)); results=xQueueCreate(1,sizeof(adsb_enrichment_t));
 current=(adsb_snapshot_t*)heap_caps_calloc(1,sizeof(*current),MALLOC_CAP_SPIRAM);
 staging=(adsb_snapshot_t*)heap_caps_calloc(1,sizeof(*staging),MALLOC_CAP_SPIRAM);
 if(!mutex || !requests || !results || !current || !staging) return false;
 nvs_handle_t h;
 if(nvs_open("siviqt_adsb",NVS_READONLY,&h)==ESP_OK) {
   char record[1024]; size_t n=sizeof(record); JsonDocument doc(&allocator);
   adsb_settings_t loaded;
   if(nvs_get_str(h,"settings",record,&n)==ESP_OK && !deserializeJson(doc,record) && settings_from_json(doc,loaded)) settings=loaded;
   nvs_close(h);
 }
 running=true;
 if(xTaskCreatePinnedToCore(worker,"adsb_data",16384,nullptr,3,nullptr,0)!=pdPASS) { running=false; return false; }
 return true;
}

double adsb_distance_nm(double lat,double lon,const adsb_settings_t *home) { return model_distance_nm(lat,lon,home); }

void adsb_diagnostics_copy(adsb_diagnostics_t *out) {
 if(!out) return;
 if(!mutex) {*out={};return;}
 xSemaphoreTake(mutex,portMAX_DELAY);*out=diagnostics;xSemaphoreGive(mutex);
}
void adsb_errors_clear(void) {
 if(!mutex) return;
 xSemaphoreTake(mutex,portMAX_DELAY);diagnostics.error_count=0;xSemaphoreGive(mutex);
}
