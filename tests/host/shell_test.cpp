#include <cassert>
#include <string>
#include <vector>
#include "ui/shell.cpp"
#include "data/model.h"
namespace adsb_maps {
unsigned sd_requests=0;void request_sd(){sd_requests++;}
const lv_image_dsc_t *test_image=nullptr;Request test_request={};
const lv_image_dsc_t *image(Request r,bool enabled,Request *display,bool){if(display)*display=r;return enabled && same(r,test_request)?test_image:nullptr;}
const char *attribution(){return "Synthetic test tiles";}
const char *status(){return "World map: SD";}
}
namespace adsb_cities {
Results queued{};bool have_results=false;uint32_t requested=0;
bool request(const char *,const char *,uint32_t token){requested=token;return true;}
bool receive(Results &out){if(!have_results)return false;out=queued;have_results=false;return true;}
}
uint64_t test_time=10000;
adsb_settings_t test_settings=default_settings();
adsb_snapshot_t test_snapshot={};
unsigned saves=0;
bool adsb_settings_get(adsb_settings_t *s){*s=test_settings;return true;}
bool adsb_settings_save(const adsb_settings_t *s){if(!valid_settings(*s))return false;test_settings=*s;saves++;return true;}
bool adsb_snapshot_copy(adsb_snapshot_t *s){*s=test_snapshot;return true;}
void adsb_service_status(char *s,size_t n){snprintf(s,n,"Test service ready");}
bool adsb_enrich_request(const char *,const char *,unsigned){return true;}
adsb_enrichment_t test_enrichment={};bool enrichment_available=false;
bool adsb_enrich_receive(adsb_enrichment_t *out){if(!enrichment_available)return false;*out=test_enrichment;enrichment_available=false;return true;}
void adsb_health_read(adsb_health_t *out){*out={};out->battery_percent=72;out->battery_mv=3900;out->connected=true;out->rssi=-55;snprintf(out->ip,20,"192.168.1.9");}
adsb_diagnostics_t test_diagnostics={};
void adsb_diagnostics_copy(adsb_diagnostics_t *out){*out=test_diagnostics;}
void adsb_errors_clear(){test_diagnostics.error_count=0;}
static std::vector<uint16_t> pixels(800*480);
static uint64_t flushed_pixels=0;
static unsigned sweep_line_tasks=0;
static void sweep_task(lv_event_t *event) {
    if(lv_draw_task_get_type(lv_event_get_draw_task(event))==LV_DRAW_TASK_TYPE_LINE)sweep_line_tasks++;
}
static void flush(lv_display_t *d,const lv_area_t *area,uint8_t *buffer) {
    flushed_pixels+=(area->x2-area->x1+1)*(area->y2-area->y1+1);
    auto src=(uint16_t*)buffer;
    for(int y=area->y1;y<=area->y2;y++)for(int x=area->x1;x<=area->x2;x++)pixels[y*800+x]=*src++;
    lv_display_flush_ready(d);
}
void frame() {dirty=true;test_time+=100;adsb_ui_tick();lv_tick_inc(100);lv_timer_handler();}
void screenshot(const char *path) {
    frame();lv_refr_now(nullptr);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n800 480\n255\n");
    for(auto p:pixels){unsigned char rgb[]={(unsigned char)((p>>11)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};fwrite(rgb,1,3,f);}fclose(f);
}
void key(bsp_input_navigation_key_t k,bool shift=false) {
    bsp_input_event_t e={};e.type=INPUT_EVENT_TYPE_NAVIGATION;e.args_navigation={k,shift?BSP_INPUT_MODIFIER_SHIFT:0u,true};
    adsb_ui_input(&e);e.args_navigation.state=false;adsb_ui_input(&e);frame();
}
void text(const char *s,char ascii=0) {
    bsp_input_event_t e={};e.type=INPUT_EVENT_TYPE_KEYBOARD;snprintf(e.args_keyboard.utf8,7,"%s",s);e.args_keyboard.ascii=ascii?ascii:s[0];adsb_ui_input(&e);frame();
}
int main(int argc,char **argv) {
    assert(argc==2);
    // Compare the float radar projector against an independent great-circle reference.
    for(double latitude:{-79.,0.,42.6975,79.})for(unsigned zoom:{0u,4u,5u}) {
        Navigation geometry;geometry.view=View::Radar;geometry.center_lat=latitude;geometry.center_lon=179.9;
        geometry.settings.preferences.radar_zoom=zoom;RadarProjection projector(geometry);
        for(unsigned bearing=0;bearing<360;bearing+=15) {
            constexpr double rad=.017453292519943295;
            double phi=latitude*rad,heading=bearing*rad,angular=geometry.range()/3440.065;
            double target=asin(sin(phi)*cos(angular)+cos(phi)*sin(angular)*cos(heading));
            double longitude=geometry.center_lon+atan2(sin(heading)*sin(angular)*cos(phi),cos(angular)-sin(phi)*sin(target))/rad;
            auto projected=projector(target/rad,longitude);
            assert(fabs(projected.x-(400+199*sin(heading)))<.1);
            assert(fabs(projected.y-(209-199*cos(heading)))<.1);
        }
    }
    lv_init();auto d=lv_display_create(800,480);static uint16_t buffer[800*24];
    lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(d,buffer,nullptr,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);
    test_settings.lat=42.7;test_settings.lon=23.3;test_settings.configured=true;
    test_snapshot.home=test_settings;test_snapshot.count=2;test_snapshot.updated_ms=9000;
    for(unsigned i=0;i<2;i++){auto &a=test_snapshot.aircraft[i];snprintf(a.hex,7,"abcde%u",i);snprintf(a.callsign,9,"TEST%u",i);a.lat=42.71+i*.01;a.lon=23.31;a.speed=350;}
    adsb_ui_init(true);frame();assert(nav.count==2);key(BSP_INPUT_NAVIGATION_KEY_DOWN);assert(!strcmp(nav.selected,"abcde0"));
    // Render an actual RGB565 background through the production draw path.
    std::vector<uint16_t> map_pixels(800*418,0xf800);
    lv_image_dsc_t background={};background.header.magic=LV_IMAGE_HEADER_MAGIC;
    background.header.cf=LV_COLOR_FORMAT_RGB565;background.header.w=800;background.header.h=418;
    background.header.stride=1600;background.data_size=map_pixels.size()*2;background.data=(const uint8_t*)map_pixels.data();
    adsb_maps::test_request={nav.center_lat,nav.center_lon,nav.range()};adsb_maps::test_image=&background;
    screenshot((std::string(argv[1])+"/map-assets.ppm").c_str());
    assert(pixels[(30+250)*800+650]==0xf800);
    nav.centered=false;nav.center_lat+=.1;frame();lv_refr_now(nullptr);
    assert(pixels[(30+250)*800+650]!=0xf800); // Mismatched center is immediately plain.
    unsigned old_map_zoom=nav.zoom();nav.recenter();nav.step_range(1);frame();lv_refr_now(nullptr);
    assert(pixels[(30+250)*800+650]!=0xf800); // Mismatched range is immediately plain.
    nav.settings.preferences.map_zoom=old_map_zoom;adsb_maps::test_image=nullptr;frame();
    std::swap(test_snapshot.aircraft[0],test_snapshot.aircraft[1]);test_time+=500;frame();assert(!strcmp(nav.selected,"abcde0"));assert(nav.selected_row(*snapshot)>=0);
    key(BSP_INPUT_NAVIGATION_KEY_RETURN);assert(nav.modal==Modal::Detail);
    // Repeated scrolling must stop at the information edges, including short content.
    for(unsigned i=0;i<40;i++)dispatch(Key::Down);
    assert(lv_obj_get_scroll_y(panel)>=0 && lv_obj_get_scroll_y(panel)<=std::max<int32_t>(0,lv_obj_get_scroll_top(panel)+lv_obj_get_scroll_bottom(panel)));
    for(unsigned i=0;i<40;i++)dispatch(Key::Up);
    assert(lv_obj_get_scroll_y(panel)==0);
    assert(!lv_obj_has_flag(panel,LV_OBJ_FLAG_OVERFLOW_VISIBLE));
    std::string long_detail;
    for(unsigned i=0;i<60;i++)long_detail+="Detail field\n";
    lv_label_set_text(detail_text,long_detail.c_str());
    lv_refr_now(nullptr);auto before_scroll=pixels;
    for(unsigned i=0;i<80;i++)dispatch(Key::Down);
    assert(lv_obj_get_scroll_y(panel)>0 && lv_obj_get_scroll_bottom(panel)>=0);
    lv_refr_now(nullptr);
    for(int y=0;y<480;y++)if(y<38 || y>=398)for(int x=0;x<800;x++)assert(pixels[y*800+x]==before_scroll[y*800+x]);
    frame(); // Live detail becomes short again: the previous offset must be clamped.
    assert(lv_obj_get_scroll_y(panel)<=std::max<int32_t>(0,lv_obj_get_scroll_top(panel)+lv_obj_get_scroll_bottom(panel)));
    unsigned token=nav.detail_token;key(BSP_INPUT_NAVIGATION_KEY_F1);assert(nav.modal==Modal::None);assert(nav.detail_token>token);
    unsigned range_before=nav.range();key(BSP_INPUT_NAVIGATION_KEY_RIGHT);assert(nav.range()<range_before);
    key(BSP_INPUT_NAVIGATION_KEY_LEFT);assert(nav.range()==range_before);
    text("=");assert(nav.range()<range_before);text("-");assert(nav.range()==range_before);
    nav.centered=false;snprintf(nav.tracking,7,"abcde0");text("h");assert(nav.centered && !*nav.tracking);
    key(BSP_INPUT_NAVIGATION_KEY_F5);assert(nav.modal==Modal::Settings);assert(editor());
    unsigned old_view=(unsigned)nav.view,old_zoom=nav.zoom();
    lv_textarea_set_text(fields[0],"");text("1");text("+");text("/");text("é");
    assert(std::string(lv_textarea_get_text(fields[0]))=="1+/é");assert((unsigned)nav.view==old_view && nav.zoom()==old_zoom && nav.modal==Modal::Settings);
    // BSP navigation+text Backspace must delete exactly once, including one UTF-8 code point.
    key(BSP_INPUT_NAVIGATION_KEY_BACKSPACE);text("\b");assert(std::string(lv_textarea_get_text(fields[0]))=="1+/");
    key(BSP_INPUT_NAVIGATION_KEY_F3);assert(nav.modal==Modal::Settings);assert((unsigned)nav.view==old_view);
    key(BSP_INPUT_NAVIGATION_KEY_TAB);text("\t");assert(lv_group_get_focused(group)==fields[1]);
    key(BSP_INPUT_NAVIGATION_KEY_TAB,true);assert(lv_group_get_focused(group)==fields[0]);
    lv_textarea_set_text(fields[0],"1234");key(BSP_INPUT_NAVIGATION_KEY_LEFT);text("-");assert(std::string(lv_textarea_get_text(fields[0]))=="123-4");
    key(BSP_INPUT_NAVIGATION_KEY_HOME);text("9");assert(std::string(lv_textarea_get_text(fields[0]))=="9123-4");
    lv_textarea_set_text(fields[0],"42.750");lv_textarea_set_text(fields[1],"23.4");
    perform(Metric);assert(std::string(lv_textarea_get_text(fields[0]))=="42.750");
    perform(FeedProvider);assert(draft.feed_provider==1 && nav.settings.feed_provider==0);
    assert(std::string(lv_textarea_get_text(fields[0]))=="42.750");
    perform(FeedProvider);assert(draft.feed_provider==0);
    lv_textarea_set_text(fields[2],"999");perform(Save);assert(nav.modal==Modal::Settings && saves==0);
    lv_textarea_set_text(fields[2],"150");perform(Save);assert(nav.modal==Modal::None && saves==1 && test_settings.lat==42.75);
    test_snapshot.home=test_settings;frame();
    // City selection edits only the draft and preserves the rest of Settings.
    key(BSP_INPUT_NAVIGATION_KEY_F5);lv_textarea_set_text(fields[2],"100");perform(FindCity);
    assert(nav.modal==Modal::Cities);lv_textarea_set_text(fields[0],"Lo");text("n");frame();frame();
    assert(adsb_cities::requested==city_token);
    adsb_cities::queued={};adsb_cities::queued.token=city_token;adsb_cities::queued.available=true;adsb_cities::queued.count=1;
    auto &city=adsb_cities::queued.cities[0];city.id=2643743;city.lat=51.50853;city.lon=-.12574;
    snprintf(city.name,sizeof(city.name),"London");snprintf(city.country,sizeof(city.country),"United Kingdom");
    adsb_cities::have_results=true;frame();assert(city_matches.count==1);
    screenshot((std::string(argv[1])+"/city-search.ppm").c_str());
    unsigned saved_before_city=saves;perform(CitySelectBase);assert(nav.modal==Modal::Settings && saves==saved_before_city);
    assert(!strcmp(lv_textarea_get_text(fields[2]),"100"));perform(Close);assert(test_settings.lat==42.75);
    // Closing a lookup invalidates its result token; late messages cannot change home.
    key(BSP_INPUT_NAVIGATION_KEY_F5);perform(FindCity);auto closed_token=city_token;key(BSP_INPUT_NAVIGATION_KEY_F1);
    assert(nav.modal==Modal::Settings && city_token>closed_token);adsb_cities::queued.token=closed_token;adsb_cities::have_results=true;frame();assert(nav.modal==Modal::Settings);perform(Close);
    // Saving a selected city persists its ID and label; manual coordinates clear them.
    key(BSP_INPUT_NAVIGATION_KEY_F5);perform(FindCity);city_matches=adsb_cities::queued;city_matches.count=1;city_pending=false;
    perform(CitySelectBase);perform(Save);assert(test_settings.city_id==2643743 && !strcmp(test_settings.city_name,"London"));
    key(BSP_INPUT_NAVIGATION_KEY_F5);lv_textarea_set_text(fields[0],"42.75");lv_textarea_set_text(fields[1],"23.4");perform(Save);
    assert(test_settings.city_id==0 && !*test_settings.city_name);test_snapshot.home=test_settings;frame();
    key(BSP_INPUT_NAVIGATION_KEY_F6);assert(nav.modal==Modal::Menu);
    assert(adsb_maps::sd_requests==0);auto view_before_sd=nav.view;
    perform(LoadSdMaps);frame();assert(adsb_maps::sd_requests==1 && nav.modal==Modal::None && nav.view==view_before_sd);
    key(BSP_INPUT_NAVIGATION_KEY_F6);key(BSP_INPUT_NAVIGATION_KEY_F1);
    key(BSP_INPUT_NAVIGATION_KEY_F4);perform(FilterBase+2);assert(nav.count==2);frame();assert(nav.count==0 && nav.selection_lost);
    key(BSP_INPUT_NAVIGATION_KEY_RETURN);assert(nav.modal==Modal::None); // Never open a replacement aircraft.
    nav.filter=0;nav.selection_lost=false;nav.selected[0]=0;frame();
    screenshot((std::string(argv[1])+"/shell.ppm").c_str());
    key(BSP_INPUT_NAVIGATION_KEY_F5);screenshot((std::string(argv[1])+"/settings.ppm").c_str());
    perform(ShowHelp);
    for(unsigned i=0;i<40;i++)dispatch(Key::Down);
    assert(lv_obj_get_scroll_bottom(panel)>=0 && lv_obj_get_scroll_y(panel)>0);
    for(unsigned i=0;i<40;i++)dispatch(Key::Up);
    assert(lv_obj_get_scroll_y(panel)==0);
    screenshot((std::string(argv[1])+"/help.ppm").c_str());perform(Close);
    // Phase 4: exercise all complete views against the same 200-aircraft snapshot.
    nav.settings.preferences.cycle=false;nav.settings.preferences.reduce_motion=true;
    nav.settings.preferences.alert_autofocus=false;nav.settings.preferences.alert_emergency=true;
    nav.settings.preferences.alert_military=true;nav.settings.preferences.trails=true;
    nav.settings.preferences.trail_points=60;nav.settings.preferences.arrivals_filter=4;
    nav.settings.preferences.map_zoom=0;nav.settings.preferences.radar_zoom=0;
    test_snapshot.home=nav.settings;test_snapshot.count=200;test_snapshot.updated_ms=test_time;
    for(unsigned i=0;i<200;i++) {
        auto &a=test_snapshot.aircraft[i];a={};snprintf(a.hex,7,"%06x",i+1);snprintf(a.callsign,9,"BAW%05u",i);
        snprintf(a.type,8,"A388");snprintf(a.registration,16,"G-TEST");snprintf(a.description,40,"Airbus A380 passenger aircraft");
        snprintf(a.operator_name,32,"Example airline operator");snprintf(a.category,3,"A5");
        a.lat=nav.settings.lat+((int)(i%20)-10)*.07;a.lon=nav.settings.lon+((int)(i/20)-5)*.15;
        a.altitude=100000-i*500;a.speed=4000-i*20;a.heading=i%360;a.vertical_rate=-12345;
        a.ias=250;a.tas=420;a.mach=.82;a.roll=4.5;a.nav_qnh=1013.2;a.nav_altitude=35000;a.seen_ms=test_time;
        a.trail_count=60;for(unsigned j=0;j<60;j++)a.trail[j]={(float)(a.lat-j*.002),(float)(a.lon-j*.002),a.altitude};
    }
    test_time+=600;frame();nav.direct_view(0);nav.rebuild(*snapshot);nav.move(*snapshot,1);frame();
    auto map_position=project(nav.settings.lat,nav.settings.lon,nav);assert(map_position.x==400 && map_position.y==209);
    assert(!lv_obj_has_flag(airspace,LV_OBJ_FLAG_HIDDEN));
    screenshot((std::string(argv[1])+"/map.ppm").c_str());
    nav.direct_view(1);nav.rebuild(*snapshot);nav.move(*snapshot,1);frame();
    assert(lv_obj_has_flag(radar_base,LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(radar_motion,LV_OBJ_FLAG_HIDDEN));
    auto radar_position=project(nav.settings.lat,nav.settings.lon,nav);assert(radar_position.x==400 && radar_position.y==209);
    screenshot((std::string(argv[1])+"/radar.ppm").c_str());
    unsigned radar_builds=radar_scene.builds;nav.settings.preferences.reduce_motion=false;
    frame();lv_refr_now(nullptr);flushed_pixels=0;
    lv_obj_add_flag(radar_motion,LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    lv_obj_add_event_cb(radar_motion,sweep_task,LV_EVENT_DRAW_TASK_ADDED,nullptr);
    for(unsigned i=0;i<20;i++)frame();
    fprintf(stderr,"Radar animation: %llu pixels over 20 frames (200 aircraft)\n",(unsigned long long)flushed_pixels);
    assert(flushed_pixels<20*400*418/2); // Bounded sweep, including periodic UI refreshes.
    assert(radar_scene.builds==radar_builds); // Sweep uses cached trails/projections.
    assert(sweep_line_tasks==0); // Animation submits bitmap copies, no software line tasks.
    // A full turn and motion disable must leave the aircraft/base bitmap exact, with no sweep ghosts.
    for(unsigned elapsed=0;elapsed<6000;elapsed+=40)radar_scene.animate(radar_motion,test_time+elapsed,true);
    radar_scene.animate(radar_motion,test_time+6000,false);
    assert(!memcmp(lv_canvas_get_draw_buf(radar_motion)->data,lv_canvas_get_draw_buf(radar_base)->data,400*418*2));
    nav.move(*snapshot,1);frame();assert(radar_scene.builds==radar_builds+1);radar_builds++;
    nav.settings.preferences.radar_zoom=1;frame();assert(radar_scene.builds==radar_builds+1);
    nav.settings.preferences.radar_zoom=0;nav.settings.preferences.reduce_motion=true;frame();

    nav.direct_view(2);nav.rebuild(*snapshot);frame();assert(nav.count==200);
    // Worst accepted numeric values must fit every primary cell in metric mode.
    // Include the maximum width accepted by the feed's bounded identity/type fields.
    auto widest=nav.visible[0];snprintf(snapshot->aircraft[widest].callsign,9,"WWWWWWWW");snprintf(snapshot->aircraft[widest].type,8,"WWWWWWW");refresh();lv_obj_update_layout(content);
    for(unsigned i=0;i<10;i++)for(unsigned c=0;c<7;c++) {
        lv_point_t extent;lv_text_get_size(&extent,lv_label_get_text(cells[i][c]),lv_obj_get_style_text_font(cells[i][c],0),0,0,10000,LV_TEXT_FLAG_NONE);
        if(extent.x>lv_obj_get_width(cells[i][c])-5)fprintf(stderr,"Cell %u,%u text %s: %d vs %d\n",i,c,lv_label_get_text(cells[i][c]),extent.x,lv_obj_get_width(cells[i][c]));
        assert(extent.x<=lv_obj_get_width(cells[i][c])-5);
    }
    for(unsigned i=0;i<10;i++){dispatch(Key::Down);}frame();assert(nav.selected_row(*snapshot)==9);
    dispatch(Key::Down);frame();assert(nav.selected_row(*snapshot)==10);
    screenshot((std::string(argv[1])+"/arrivals.ppm").c_str());
    dispatch(Key::Right);frame();assert(nav.page==1);
    screenshot((std::string(argv[1])+"/arrivals-extended.ppm").c_str());
    dispatch(Key::Enter);frame();assert(nav.modal==Modal::Detail);
    snprintf(test_enrichment.hex,7,"%s",nav.detail);snprintf(test_enrichment.callsign,9,"%s",nav.find(*snapshot,nav.detail)->callsign);
    test_enrichment.token=nav.detail_token;test_enrichment.valid=true;
    snprintf(test_enrichment.origin,8,"SOF");snprintf(test_enrichment.destination,8,"LHR");snprintf(test_enrichment.credit,96,"Example photographer / photo credit");
    snprintf(test_enrichment.operator_name,64,"Enriched aircraft operator");enrichment_available=true;test_time+=600;frame();
    auto full_detail=std::string(lv_label_get_text(detail_text))+lv_label_get_text(detail_right);
    assert(full_detail.find("SOF > LHR")!=std::string::npos && full_detail.find("Example photographer")!=std::string::npos);
    assert(full_detail.find("Enriched aircraft operator")!=std::string::npos);
    for(auto field:{"Operator:","Registration:","IAS:","TAS:","Mach:","Roll:","QNH:","Selected altitude:","PHOTO CREDIT"})assert(full_detail.find(field)!=std::string::npos);
    screenshot((std::string(argv[1])+"/detail.ppm").c_str());
    for(unsigned i=0;i<100;i++){dispatch(Key::Down);}frame();
    assert(lv_obj_get_scroll_y(panel)>0 && lv_obj_get_scroll_bottom(panel)>=0);
    screenshot((std::string(argv[1])+"/detail-bottom.ppm").c_str());close();
    // A completion for a closed/reopened detail token must not update its screen or cache.
    nav.rebuild(*snapshot);assert(nav.open_detail(*snapshot));request_detail();render_modal();
    snprintf(test_enrichment.credit,96,"STALE completion");enrichment_available=true;test_time+=600;frame();
    assert(std::string(lv_label_get_text(detail_right)).find("STALE completion")==std::string::npos);close();
    nav.direct_view(3);frame();assert(!lv_obj_has_flag(stats_panel,LV_OBJ_FLAG_HIDDEN));
    auto report_text=stats_report;
    for(auto section:{"TRAFFIC","CURRENT RECORDS","SESSION","SYSTEM","NETWORK"})assert(report_text.find(section)!=std::string::npos);
    screenshot((std::string(argv[1])+"/stats.ppm").c_str());
    unsigned before_stats=stats_builds;
    for(unsigned i=0;i<20;i++){dispatch(Key::Down);dispatch(Key::Up);}
    assert(stats_builds==before_stats); // Scroll input does not reformat reports.

    for(unsigned i=0;i<100;i++){dispatch(Key::Down);}frame();assert(lv_obj_get_scroll_y(stats_panel)>0 && lv_obj_get_scroll_bottom(stats_panel)>=0);
    screenshot((std::string(argv[1])+"/stats-bottom.ppm").c_str());
    // Incoming alerts never change a settings editor's contents or focus.
    perform(ShowSettings);auto input_focus=lv_group_get_focused(group);auto saved_text=std::string(lv_textarea_get_text(fields[0]));
    test_snapshot.aircraft[0].emergency=true;test_snapshot.aircraft[0].squawk=7700;
    test_time+=600;frame();unsigned alerts_before=alert_count;assert(alerts_before>0);
    assert(nav.modal==Modal::Settings && lv_group_get_focused(group)==input_focus && std::string(lv_textarea_get_text(fields[0]))==saved_text);
    test_time+=600;frame();assert(alert_count==alerts_before);screenshot((std::string(argv[1])+"/alert-editing.ppm").c_str());
    close();perform(ShowAlerts);frame();screenshot((std::string(argv[1])+"/alerts.ppm").c_str());
    nav.filter=7;snprintf(nav.query,80,"unmatched");perform(AlertOpenBase+alert_count-1);frame();assert(nav.modal==Modal::Detail && !strcmp(nav.detail,test_snapshot.aircraft[0].hex));close();
    perform(ShowAlerts);perform(DismissAlerts);assert(alerts[alert_count-1].dismissed && toast_until==0);close();
    // Autofocus is allowed only in browsing, never over a deliberate selection.
    nav.settings.preferences.alert_autofocus=true;nav.selected[0]=0;nav.selection_lost=false;nav.direct_view(3);
    test_snapshot.aircraft[1].military=true;test_time+=600;frame();assert(nav.view==View::Map && !strcmp(nav.tracking,test_snapshot.aircraft[1].hex));
    test_diagnostics.error_count=1;snprintf(test_diagnostics.errors[0].text,160,"TLS certificate/handshake failure; cached aircraft retained");
    perform(ShowErrors);frame();screenshot((std::string(argv[1])+"/errors.ppm").c_str());perform(ClearErrors);assert(test_diagnostics.error_count==0);close();
    nav.settings.preferences.alert_autofocus=false;toast_until=0;feedback_until=0;feedback[0]=0;nav.recenter();nav.direct_view(2);
    test_snapshot.count=0;test_snapshot.updated_ms=0;test_time+=600;frame();assert(std::string(lv_label_get_text(notice)).find("Loading")!=std::string::npos);
    screenshot((std::string(argv[1])+"/loading.ppm").c_str());
    test_snapshot.updated_ms=test_time;test_time+=600;frame();assert(std::string(lv_label_get_text(notice)).find("No aircraft reported")!=std::string::npos);
    // Expected cadence is the active session interval, not the faster saved preference.
    nav.direct_view(0);nav.recenter();test_snapshot.count=1;test_snapshot.updated_ms=test_time-20000;
    test_diagnostics.effective_poll_seconds=30;diagnostics=test_diagnostics;frame();
    assert(std::string(lv_label_get_text(notice)).find("STALE DATA")==std::string::npos);
    test_time+=100000;frame();assert(std::string(lv_label_get_text(notice)).find("STALE DATA")!=std::string::npos);
    nav.selected[0]=0;nav.selection_lost=false;key(BSP_INPUT_NAVIGATION_KEY_F1);assert(nav.modal==Modal::Exit);assert(!adsb_ui_take_exit());
    key(BSP_INPUT_NAVIGATION_KEY_TAB);key(BSP_INPUT_NAVIGATION_KEY_RETURN);assert(adsb_ui_take_exit());
    puts("Phase 4 screen layouts, geometry, complete details, 200-aircraft paging, Stats scrolling, alert identity/focus, loading/errors, LVGL shell routing, UTF-8 editing, focus, settings, identity and exit checks passed");
}
