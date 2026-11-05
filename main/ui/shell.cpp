#include "shell.h"
#include "navigation.h"
#include "layout.h"
#include "screens.h"
#include "radar_scene.h"
#include "platform.h"
#include "maps.h"
#include "cities.h"
#include "lvgl.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <cstdlib>
#include <cerrno>

namespace {
using namespace adsb_ui;
Navigation nav;
KeyGate gate;
adsb_snapshot_t *snapshot;
adsb_settings_t draft;
char saved_settings_fields[4][80]={},city_query[80]={},city_country_filter[4]={};
adsb_cities::Results city_matches{};
uint32_t city_token=0;uint64_t city_due=0;bool city_pending=false;
Navigation map_draw_nav;
lv_group_t *group;
lv_obj_t *heading, *footer, *content, *summary, *rows[Layout::rows], *notice, *modal, *panel, *detail_text, *detail_right, *modal_notice;
lv_obj_t *fields[4]={};
lv_obj_t *airspace,*board_header,*cells[Layout::rows][7],*column_titles[7],*stats_panel,*stats_text,*toast;
Session session;
RadarScene radar_scene;
lv_obj_t *radar_base,*radar_motion;
uint64_t stats_due=0;std::string stats_key;
unsigned stats_builds=0;
lv_obj_t *stats_sections[5];std::string stats_report;
adsb_health_t health={};adsb_diagnostics_t diagnostics={};
struct Route {adsb_enrichment_t data;uint64_t requested;};
Route *routes;unsigned route_slot=0;uint64_t next_route=0,health_due=0,animation_due=0;
struct Alert {char hex[7],title[80];uint64_t at,id;bool dismissed;};
Alert alerts[32]={};unsigned alert_count=0;
char alert_targets[32][7]={};uint64_t alert_ids[32]={},next_alert_id=1;
struct AlertSeen {char hex[7];unsigned flags;};
AlertSeen alert_seen[2000]={};unsigned alert_seen_count=0;
uint64_t toast_until=0;char toast_text[128]={};

const int column_widths[]={136,136,120,90,74,76,144};
Route *route_for(const adsb_aircraft_t &a) {
    for(unsigned i=0;i<200;i++)if(!strcmp(routes[i].data.hex,a.hex) && !strcmp(routes[i].data.callsign,a.callsign))return &routes[i];
    return nullptr;
}

char network[160]="Wi-Fi: starting", feedback[160]="", enrichment[384]="";
bool ready, reconnect_requested=false, exit_requested=false, dirty=true, preferences_dirty=false;
uint64_t next_refresh=0, feedback_until=0;
int focus_index=0;

enum Action { Close=1, Previous, Next, ShowFilters, ShowSettings, ShowMenu, ShowSearch, ShowHelp, ShowExit,
    SortDirection, ArrivalsRange, ExitNow, Reconnect, ZoomIn, ZoomOut, Recenter, Track, Cycle, Sorting, ClearSearch, Save, ApplySearch,
    Metric, Trails, TrailPoints, TrailStyle, CycleSeconds, IdleSeconds, Military, Emergency, Autofocus, ReduceMotion, ShowAlerts, ShowErrors, ClearErrors, DismissAlerts, LoadSdMaps, FeedProvider, AlertOpenBase=200, AlertDismissBase=240, RefreshHistory=280, FindCity=290, CityBack, CitySelectBase=320,
    FilterBase=100 };
uint64_t now_ms() {return esp_timer_get_time()/1000;}
void request_detail() {
    gate.cancel_repeat();snprintf(enrichment,sizeof(enrichment),"Loading route, aircraft metadata and photo credit...");
    auto a=nav.find(*snapshot,nav.detail);
    if(!a || !adsb_enrich_request(a->hex,a->callsign,nav.detail_token))snprintf(enrichment,sizeof(enrichment),"Enrichment unavailable or queue busy; reopen to retry");
}

void render_modal();
void refresh();
void perform(int action);

void set_label(lv_obj_t *o,const char *text) {if(strcmp(lv_label_get_text(o),text))lv_label_set_text(o,text);}
void set_font(lv_obj_t *o,const lv_font_t *font) {if(lv_obj_get_style_text_font(o,0)!=font)lv_obj_set_style_text_font(o,font,0);}
void set_color(lv_obj_t *o,uint32_t value) {auto color=lv_color_hex(value);if(!lv_color_eq(lv_obj_get_style_text_color(o,0),color))lv_obj_set_style_text_color(o,color,0);}
lv_obj_t *label(lv_obj_t *parent, const char *text) {
    auto o=lv_label_create(parent); lv_label_set_text(o,text); lv_obj_set_width(o,LV_PCT(100));
    lv_label_set_long_mode(o,LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(o,lv_color_hex(0xe5edf4),0); return o;
}
void plain(lv_obj_t *o) {
    lv_obj_set_style_radius(o,0,0); lv_obj_set_style_border_width(o,0,0);
    lv_obj_set_style_pad_all(o,0,0); lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);lv_obj_remove_flag(o,LV_OBJ_FLAG_OVERFLOW_VISIBLE);
}
void focused(lv_event_t *e) {
    auto o=(lv_obj_t*)lv_event_get_target(e); lv_obj_scroll_to_view(o,LV_ANIM_OFF);
}
void add_focus(lv_obj_t *o) {
    lv_group_add_obj(group,o);
    lv_obj_set_style_outline_color(o,lv_color_hex(0x55ddff),LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(o,3,LV_STATE_FOCUSED);
    lv_obj_add_event_cb(o,focused,LV_EVENT_FOCUSED,nullptr);
}
void clicked(lv_event_t *e) {perform((int)(intptr_t)lv_event_get_user_data(e));}
lv_obj_t *button(const char *text,int action) {
    auto b=lv_button_create(panel); lv_obj_set_size(b,LV_PCT(100),38);
    lv_obj_set_style_bg_color(b,lv_color_hex(0x253b50),0);
    auto l=label(b,text); lv_obj_center(l); add_focus(b);
    lv_obj_add_event_cb(b,clicked,LV_EVENT_CLICKED,(void*)(intptr_t)action); return b;
}
void edit(unsigned index,const char *name,const char *text) {
    label(panel,name);
    auto o=lv_textarea_create(panel); fields[index]=o;
    lv_obj_set_width(o,LV_PCT(100)); lv_textarea_set_one_line(o,true); lv_textarea_set_max_length(o,79);
    lv_textarea_set_text(o,text); add_focus(o);
}
bool editor() {auto o=lv_group_get_focused(group); return o && lv_obj_check_type(o,&lv_textarea_class);}
void message(const char *s) {snprintf(feedback,sizeof(feedback),"%s",s);feedback_until=now_ms()+5000;dirty=true;}
void open(Modal m) {
    gate.cancel_repeat(); nav.open(m); focus_index=0; feedback[0]=0;
    if(m==Modal::Settings) draft=nav.settings;
    render_modal(); dirty=true;
}
void return_to_settings() {
    ++city_token;city_pending=false;nav.open(Modal::Settings);focus_index=0;render_modal();
    for(unsigned i=0;i<4;i++)lv_textarea_set_text(fields[i],saved_settings_fields[i]);
    dirty=true;
}
void close() {if(nav.modal==Modal::Cities){return_to_settings();return;}gate.cancel_repeat();nav.close();render_modal();dirty=true;}
void remember_focus() {
    auto selected=lv_group_get_focused(group); focus_index=0;
    for(uint32_t i=0;i<lv_group_get_obj_count(group);i++) if(lv_group_get_obj_by_index(group,i)==selected) focus_index=(int)i;
}
void render_modal() {
    lv_group_remove_all_objs(group);
    if(modal) {lv_obj_delete(modal);modal=nullptr;}
    memset(fields,0,sizeof(fields)); detail_text=nullptr;detail_right=nullptr;modal_notice=nullptr;
    if(nav.modal==Modal::None) return;
    modal=lv_obj_create(lv_screen_active()); plain(modal);
    lv_obj_set_pos(modal,0,Layout::content_y);lv_obj_set_size(modal,Layout::width,Layout::content_height);
    lv_obj_set_style_bg_color(modal,lv_color_hex(0x152331),0);
    panel=lv_obj_create(modal); plain(panel); lv_obj_add_flag(panel,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(panel,LV_OBJ_FLAG_SCROLL_ELASTIC);lv_obj_remove_flag(panel,LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_scroll_dir(panel,LV_DIR_VER);
    lv_obj_set_pos(panel,16,8);lv_obj_set_size(panel,768,360);
    modal_notice=label(modal,feedback);lv_obj_set_pos(modal_notice,16,376);lv_obj_set_size(modal_notice,768,38);lv_label_set_long_mode(modal_notice,LV_LABEL_LONG_CLIP);
    lv_obj_set_style_bg_opa(panel,LV_OPA_TRANSP,0);lv_obj_set_style_pad_all(panel,6,0);
    lv_obj_set_style_pad_row(panel,10,0);lv_obj_set_flex_flow(panel,LV_FLEX_FLOW_COLUMN);
    char text[180]; unsigned limit=0;
    switch(nav.modal) {
    case Modal::Filters:
        label(panel,"Filter aircraft - one active filter across views");
        for(unsigned i=0;i<8;i++) {snprintf(text,sizeof(text),"%s %s",nav.filter==i ? LV_SYMBOL_OK:"  ",filter_names[i]);button(text,FilterBase+i);}
        button("Cancel",Close);break;
    case Modal::Settings:
        label(panel,"Settings - Tab moves focus; Save applies all changes");
        label(panel,"LOCATION");
        if(*draft.city_name)label(panel,format("Home: %s, %s",draft.city_name,draft.city_country).c_str());
        snprintf(text,sizeof(text),"%.6f",draft.lat);edit(0,"Home latitude (-90 to 90)",draft.configured ? text:"");
        snprintf(text,sizeof(text),"%.6f",draft.lon);edit(1,"Home longitude (-180 to 180)",draft.configured ? text:"");
        button("Find city by English name",FindCity);
        label(panel,"DATA");
        snprintf(text,sizeof(text),"%u",draft.radius_nm);edit(2,"API coverage (1 to 250 nm; separate from display range)",text);
        snprintf(text,sizeof(text),"%u",draft.poll_seconds);edit(3,"Polling interval (5 to 300 seconds)",text);
        label(panel,"Server throttling may temporarily lengthen the active polling interval.");
        label(panel,"DISPLAY");
        snprintf(text,sizeof(text),"Units: %s",draft.preferences.metric?"Metric":"Aviation (ft / kt / nm)");button(text,Metric);
        snprintf(text,sizeof(text),"Trails: %s",draft.preferences.trails?"On":"Off");button(text,Trails);
        snprintf(text,sizeof(text),"Trail points: %u",draft.preferences.trail_points);button(text,TrailPoints);
        snprintf(text,sizeof(text),"Trail style: %s",draft.preferences.trail_style?"Altitude":"Solid");button(text,TrailStyle);
        snprintf(text,sizeof(text),"Reduce animation: %s",draft.preferences.reduce_motion?"On":"Off");button(text,ReduceMotion);
        snprintf(text,sizeof(text),"Auto-cycle: %s",draft.preferences.cycle?"On":"Off");button(text,Cycle);
        snprintf(text,sizeof(text),"View interval: %u seconds",draft.preferences.cycle_seconds);button(text,CycleSeconds);
        snprintf(text,sizeof(text),"Resume after: %u seconds idle",draft.preferences.cycle_idle_seconds);button(text,IdleSeconds);
        label(panel,"ALERTS - classification uses available data and heuristics");
        snprintf(text,sizeof(text),"Military alerts: %s",draft.preferences.alert_military?"On":"Off");button(text,Military);
        snprintf(text,sizeof(text),"Emergency alerts: %s",draft.preferences.alert_emergency?"On":"Off");button(text,Emergency);
        snprintf(text,sizeof(text),"Alert autofocus: %s",draft.preferences.alert_autofocus?"On":"Off");button(text,Autofocus);
        label(panel,"AIRCRAFT DATA SOURCE");
        button(draft.feed_provider==1?"Source: adsb.fi (change)":"Source: adsb.lol (change)",FeedProvider);
        label(panel,draft.feed_provider==1?"Data: https://adsb.fi - personal, non-commercial use only":"Data: https://adsb.lol");
        label(panel,"NETWORK");label(panel,network);button("Reconnect using launcher Wi-Fi profiles",Reconnect);
        label(panel,"To edit Wi-Fi profiles, return to the launcher Wi-Fi setup.");
        button("Save settings",Save);button("Cancel changes",Close);break;
    case Modal::Cities:
        label(panel,"Find city - English names | GeoNames: geonames.org | CC BY 4.0");
        edit(0,"City name (at least 2 characters)",city_query);
        edit(1,"Country code (optional, e.g. BG, GB, US)",city_country_filter);
        lv_textarea_set_max_length(fields[1],2);
        if(city_pending)label(panel,"Searching SD city database...");
        else if(strlen(city_query)<2)label(panel,"Type a city name, then Tab to choose a result.");
        else if(!city_matches.available)label(panel,"City database unavailable. Insert the app SD card; F6 can retry SD assets.");
        else if(!city_matches.count)label(panel,"No matching cities. Refine the name or country code.");
        for(unsigned i=0;i<city_matches.count;i++) {
            const auto &c=city_matches.cities[i];
            auto result=button(format("%s\n%s | region %s",c.name,c.country,*c.region?c.region:"-").c_str(),CitySelectBase+i);
            lv_obj_set_height(result,64);lv_obj_set_style_text_font(lv_obj_get_child(result,0),&lv_font_montserrat_14,0);
        }
        if(city_matches.more)label(panel,"More matches available; refine the city name or country code.");
        button("Back to location settings",CityBack);break;
    case Modal::Search:
        label(panel,"Search by callsign, registration or ICAO hex");edit(0,"Search text (case insensitive)",nav.query);
        button("Apply search",ApplySearch);button("Clear search",ClearSearch);button("Cancel",Close);break;
    case Modal::Detail:
        {
        auto columns=lv_obj_create(panel);plain(columns);lv_obj_set_width(columns,LV_PCT(100));lv_obj_set_height(columns,LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(columns,LV_OPA_TRANSP,0);lv_obj_set_flex_flow(columns,LV_FLEX_FLOW_ROW);lv_obj_set_style_pad_column(columns,16,0);
        detail_text=label(columns,"");detail_right=label(columns,"");lv_obj_set_width(detail_text,354);lv_obj_set_width(detail_right,354);
        button("Track this aircraft and return to view",Track);button("Back to selection",Close);break;
        }
    case Modal::Menu:
        label(panel,"Actions");button("Previous view",Previous);button("Next view",Next);
        button("Filters",ShowFilters);button("Settings",ShowSettings);button("Search aircraft",ShowSearch);
        button("Clear search",ClearSearch);button("Zoom in (+)",ZoomIn);button("Zoom out (-)",ZoomOut);
        button("Recenter on home (H)",Recenter);button("Track selected aircraft",Track);
        snprintf(text,sizeof(text),"Sort: %s",nav.sort==Sort::Distance?"Distance":nav.sort==Sort::Callsign?"Callsign":nav.sort==Sort::Altitude?"Altitude":"Speed");button(text,Sorting);
        button(nav.descending?"Order: descending":"Order: ascending",SortDirection);
        limit=arrivals_ranges[nav.settings.preferences.arrivals_filter];
        if(limit)snprintf(text,sizeof(text),"Arrivals distance: %u nm",limit);else snprintf(text,sizeof(text),"Arrivals distance: All received aircraft");button(text,ArrivalsRange);
        snprintf(text,sizeof(text),"Auto-cycle: %s",nav.settings.preferences.cycle?"On":"Off");button(text,Cycle);
        button("Alerts - open aircraft or dismiss",ShowAlerts);button("Error history",ShowErrors);button("Clear error history",ClearErrors);
        label(panel,adsb_maps::status());button("Retry SD assets",LoadSdMaps);button("Reconnect Wi-Fi",Reconnect);button("Keyboard help",ShowHelp);button("Exit to launcher",ShowExit);button("Back",Close);break;
    case Modal::Help:
        label(panel,"Keyboard\n\nF1 / Esc: back, then exit choice\nF2 / F3: previous / next view\nF4: filters   F5: settings   F6: actions\n1 / 2 / 3 / 4: Map / Radar / Arrivals / Stats\nUp / Down: select aircraft or scroll\nEnter: open selected aircraft / activate control\nTab / Shift+Tab: next / previous control\nLeft / Right: Map/Radar range; Arrivals column page\n= / +: zoom in   -: zoom out   H: recenter   /: search\n\nWhile editing, characters go only into the focused field.\nSelection stays on the same ICAO hex across refreshes.\nAuto-cycle pauses during selection, editing and menus.\n\nMap/Radar: selected aircraft is highlighted.\nStats: Up/Down scroll all metrics.\nArrivals: Left/Right changes column page.\nF6 opens alert list and error history.\nWorldwide maps and English city search are included on the app SD card.");
        button("Back",Close);break;
    case Modal::Alerts:
        label(panel,"Alert history - newest first (last 32 events)");
        for(unsigned i=alert_count;i>0;i--) {
            auto &a=alerts[i-1];if(a.dismissed)continue;
            snprintf(alert_targets[i-1],7,"%s",a.hex);alert_ids[i-1]=a.id;
            label(panel,format("%s | %s | %llus ago",a.title,a.hex,(unsigned long long)((now_ms()-a.at)/1000)).c_str());
            button("Open on Map (clear filter and search)",AlertOpenBase+i-1);button("Dismiss",AlertDismissBase+i-1);
        }
        button("Refresh alert history",RefreshHistory);button("Dismiss all",DismissAlerts);button("Back",Close);break;
    case Modal::Errors:
        label(panel,"Error history - last 16 service failures, newest first");
        adsb_diagnostics_copy(&diagnostics);
        if(!diagnostics.error_count)label(panel,"No recorded service failures");
        for(unsigned i=diagnostics.error_count;i>0;i--)label(panel,format("At %llus: %s",(unsigned long long)(diagnostics.errors[i-1].at_ms/1000),diagnostics.errors[i-1].text).c_str());
        button("Refresh error history",RefreshHistory);button("Clear error history",ClearErrors);button("Back",Close);break;
    case Modal::Exit:
        label(panel,"Return to the launcher? Unsaved settings will be discarded.");
        button("Keep using ADS-B",Close);button("Exit to launcher",ExitNow);break;
    default:break;
    }
    unsigned n=lv_group_get_obj_count(group);
    if(n) lv_group_focus_obj(lv_group_get_obj_by_index(group,std::min((unsigned)focus_index,n-1)));
    if(nav.modal==Modal::Help || nav.modal==Modal::Detail)lv_obj_scroll_to_y(panel,0,LV_ANIM_OFF);
}
bool parse(const char *s,double &value) {
    errno=0; char *end=nullptr; value=strtod(s,&end);
    return end!=s && !*end && !errno && std::isfinite(value);
}
void perform(int action) {
    nav.activity(now_ms()); dirty=true;
    if(action>=CitySelectBase && action<CitySelectBase+20 && nav.modal==Modal::Cities) {
        unsigned i=action-CitySelectBase;if(city_pending || i>=city_matches.count)return;
        const auto &c=city_matches.cities[i];draft.lat=c.lat;draft.lon=c.lon;draft.city_id=c.id;
        snprintf(draft.city_name,sizeof(draft.city_name),"%s",c.name);snprintf(draft.city_country,sizeof(draft.city_country),"%s",c.country);
        snprintf(saved_settings_fields[0],80,"%.6f",c.lat);snprintf(saved_settings_fields[1],80,"%.6f",c.lon);
        return_to_settings();return;
    }
    if(action>=AlertDismissBase && action<AlertDismissBase+32) {
        for(auto &a:alerts)if(a.id==alert_ids[action-AlertDismissBase])a.dismissed=true;
        toast_until=0;render_modal();return;
    }
    if(action>=AlertOpenBase && action<AlertOpenBase+32) {
        auto a=nav.find(*snapshot,alert_targets[action-AlertOpenBase]);
        if(!a) {message("Aircraft no longer available; dismiss this alert to remove it");return;}
        nav.direct_view(0);nav.filter=0;nav.query[0]=0;nav.centered=false;snprintf(nav.tracking,7,"%s",a->hex);nav.rebuild(*snapshot);
        snprintf(nav.selected,7,"%s",a->hex);
        if(nav.open_detail(*snapshot)) {request_detail();focus_index=0;render_modal();}return;
    }
    if(action>=FilterBase && action<FilterBase+8) {unsigned chosen=action-FilterBase;nav.filter=nav.filter==chosen ? 0:chosen;close();return;}
    switch(action) {
    case FindCity:
        for(unsigned i=0;i<4;i++)snprintf(saved_settings_fields[i],80,"%s",lv_textarea_get_text(fields[i]));
        city_query[0]=city_country_filter[0]=0;city_matches={};++city_token;city_pending=false;
        open(Modal::Cities);return;
    case CityBack:return_to_settings();return;
    case Close: close();return;
    case Previous: nav.switch_view(-1);render_modal();return;
    case Next: nav.switch_view(1);render_modal();return;
    case ShowFilters:open(Modal::Filters);return;
    case ShowSettings:open(Modal::Settings);return;
    case ShowMenu:open(Modal::Menu);return;
    case ShowSearch:open(Modal::Search);return;
    case ShowHelp:open(Modal::Help);return;
    case ShowAlerts:open(Modal::Alerts);return;
    case RefreshHistory:render_modal();return;
    case ShowErrors:open(Modal::Errors);return;
    case ClearErrors:adsb_errors_clear();message("Error history cleared");if(nav.modal==Modal::Errors)render_modal();return;
    case DismissAlerts:for(auto &a:alerts)a.dismissed=true;toast_until=0;render_modal();return;
    case ShowExit:open(Modal::Exit);return;
    case ExitNow:
        if(preferences_dirty && ready && nav.settings.configured && !adsb_settings_save(&nav.settings)) {
            message("Preference save failed; exit cancelled. Retry or save in Settings.");return;
        }
        preferences_dirty=false;exit_requested=true;return;
    case LoadSdMaps:adsb_maps::request_sd();close();message("Retrying SD maps and city database");return;
    case Reconnect:reconnect_requested=true;message("Wi-Fi reconnect requested");return;
    case ZoomIn:nav.step_range(1);preferences_dirty=true;close();return;
    case ZoomOut:nav.step_range(-1);preferences_dirty=true;close();return;
    case Recenter:nav.recenter();close();return;
    case Track: {
        const char *hex=nav.modal==Modal::Detail ? nav.detail:nav.selected;
        auto a=nav.find(*snapshot,hex);
        if(!a) {message("Select an available aircraft first");return;}
        snprintf(nav.tracking,sizeof(nav.tracking),"%s",hex);nav.centered=false;
        nav.center_lat=a->lat;nav.center_lon=a->lon;close();return;
    }
    case Sorting:nav.sort=(Sort)(((unsigned)nav.sort+1)%4);break;
    case SortDirection:nav.descending=!nav.descending;break;
    case ArrivalsRange:nav.settings.preferences.arrivals_filter=(nav.settings.preferences.arrivals_filter+1)%6;preferences_dirty=true;break;
    case ClearSearch:nav.query[0]=0;close();return;
    case ApplySearch:snprintf(nav.query,sizeof(nav.query),"%s",lv_textarea_get_text(fields[0]));close();return;
    case Save: {
        double values[4];
        for(unsigned i=0;i<4;i++) if(!parse(lv_textarea_get_text(fields[i]),values[i])) {message("Use a valid number in each field");lv_group_focus_obj(fields[i]);return;}
        if(values[0]<-90 || values[0]>90 || values[1]<-180 || values[1]>180 || values[2]<1 || values[2]>250 ||
           values[3]<5 || values[3]>300 || floor(values[2])!=values[2] || floor(values[3])!=values[3]) {message("Invalid coordinates, coverage (1-250), or polling (5-300)");return;}
        if(fabs(draft.lat-values[0])>1e-6 || fabs(draft.lon-values[1])>1e-6) {
            draft.city_id=0;draft.city_name[0]=draft.city_country[0]=0;
        }
        draft.lat=values[0];draft.lon=values[1];draft.radius_nm=(unsigned)values[2];draft.poll_seconds=(unsigned)values[3];draft.configured=true;
        if(!ready || !adsb_settings_save(&draft)) {message("Settings could not be saved; changes remain editable");return;}
        bool changed=nav.settings.lat!=draft.lat || nav.settings.lon!=draft.lon;
        nav.settings=draft;preferences_dirty=false;if(changed) nav.recenter();message("Settings saved");close();return;
    }
    default: break;
    }
    if(action==Cycle && nav.modal!=Modal::Settings) {
        nav.settings.preferences.cycle=!nav.settings.preferences.cycle;preferences_dirty=true;
    } else if(nav.modal==Modal::Settings) {
        auto &p=draft.preferences;
        switch(action) {
        case FeedProvider:draft.feed_provider=1-draft.feed_provider;break;
        case Metric:p.metric=!p.metric;break;
        case Trails:p.trails=!p.trails;break;
        case TrailPoints:p.trail_points=p.trail_points==60?10:p.trail_points+10;break;
        case TrailStyle:p.trail_style^=1;break;
        case Cycle:p.cycle=!p.cycle;break;
        case CycleSeconds:p.cycle_seconds=p.cycle_seconds>=120?15:p.cycle_seconds+15;break;
        case IdleSeconds:p.cycle_idle_seconds=p.cycle_idle_seconds>=600?15:std::min(600u,p.cycle_idle_seconds+15);break;
        case Military:p.alert_military=!p.alert_military;break;
        case Emergency:p.alert_emergency=!p.alert_emergency;break;
        case Autofocus:p.alert_autofocus=!p.alert_autofocus;break;
        case ReduceMotion:p.reduce_motion=!p.reduce_motion;break;
        default:return;
        }
    }
    char saved[4][80]={}; bool settings=nav.modal==Modal::Settings;
    if(settings) for(unsigned i=0;i<4;i++) snprintf(saved[i],sizeof(saved[i]),"%s",lv_textarea_get_text(fields[i]));
    remember_focus();render_modal();
    if(settings) for(unsigned i=0;i<4;i++) lv_textarea_set_text(fields[i],saved[i]);
}
void send_key(uint32_t key) {
    auto o=lv_group_get_focused(group);if(o) lv_obj_send_event(o,LV_EVENT_KEY,&key);
}
void scroll_panel(int delta) {
    lv_obj_update_layout(panel);
    lv_obj_scroll_by_bounded(panel,0,delta,LV_ANIM_OFF);
}
void dispatch(Key key,bool shift=false) {
    if(nav.modal==Modal::None && nav.view==View::Stats && (key==Key::Up || key==Key::Down || key==Key::PageUp || key==Key::PageDown)) {
        nav.activity(now_ms());int step=(key==Key::PageUp || key==Key::PageDown)?240:60;
        lv_obj_scroll_by_bounded(stats_panel,0,key==Key::Up || key==Key::PageUp?step:-step,LV_ANIM_OFF);return;
    }
    nav.activity(now_ms());nav.rebuild(*snapshot);dirty=true;
    if(key==Key::Back) {
        if(nav.modal!=Modal::None) {close();return;}
        if(*nav.selected || nav.selection_lost) {nav.selected[0]=0;nav.selection_lost=false;return;}
        open(Modal::Exit);return;
    }
    if(nav.modal!=Modal::None) {
        if(key==Key::Tab) {if(shift)lv_group_focus_prev(group);else lv_group_focus_next(group);return;}
        if(key==Key::Enter) {
            if(editor()) {lv_group_focus_next(group);return;}
            auto o=lv_group_get_focused(group);if(o)lv_obj_send_event(o,LV_EVENT_CLICKED,nullptr);return;
        }
        if(editor()) {
            if(key==Key::Left)send_key(LV_KEY_LEFT);
            if(key==Key::Right)send_key(LV_KEY_RIGHT);
            if(key==Key::Home)send_key(LV_KEY_HOME);
            if(key==Key::End)send_key(LV_KEY_END);
            // Backspace comes from translated text only (BSP sends both forms).
            return;
        }
        if(key==Key::Up || key==Key::Down || key==Key::PageUp || key==Key::PageDown) {
            if(nav.modal==Modal::Detail || nav.modal==Modal::Help) scroll_panel(key==Key::Up||key==Key::PageUp?60:-60);
            else if(key==Key::Up || key==Key::PageUp)lv_group_focus_prev(group);else lv_group_focus_next(group);
        }
        if(key==Key::Left || key==Key::Right) {
            auto o=lv_group_get_focused(group);if(o && nav.modal==Modal::Settings && !editor())lv_obj_send_event(o,LV_EVENT_CLICKED,nullptr);
        }
        return; // Modal keys never reach global shortcuts.
    }
    switch(key) {
    case Key::Previous:perform(Previous);break;
    case Key::Next:perform(Next);break;
    case Key::Filters:perform(ShowFilters);break;
    case Key::Settings:perform(ShowSettings);break;
    case Key::Menu:perform(ShowMenu);break;
    case Key::Up:if(nav.view==View::Stats)lv_obj_scroll_by_bounded(stats_panel,0,60,LV_ANIM_OFF);else nav.move(*snapshot,-1);break;
    case Key::Down:if(nav.view==View::Stats)lv_obj_scroll_by_bounded(stats_panel,0,-60,LV_ANIM_OFF);else nav.move(*snapshot,1);break;
    case Key::PageUp:if(nav.view==View::Stats)lv_obj_scroll_by_bounded(stats_panel,0,240,LV_ANIM_OFF);else nav.move(*snapshot,-10);break;
    case Key::PageDown:if(nav.view==View::Stats)lv_obj_scroll_by_bounded(stats_panel,0,-240,LV_ANIM_OFF);else nav.move(*snapshot,10);break;
    case Key::Home:nav.recenter();break;
    case Key::Left:if(nav.view==View::Arrivals)nav.page=0;else {nav.step_range(-1);preferences_dirty=true;}break;
    case Key::Right:if(nav.view==View::Arrivals)nav.page=1;else {nav.step_range(1);preferences_dirty=true;}break;
    case Key::Enter:
        if(nav.open_detail(*snapshot)) {
            request_detail();
            focus_index=0;render_modal();
        } else message("Use Up / Down to select an available aircraft");
        break;
    default:break;
    }
}
Key translate(bsp_input_navigation_key_t k) {
    switch(k) {
    case BSP_INPUT_NAVIGATION_KEY_ESC:case BSP_INPUT_NAVIGATION_KEY_F1:return Key::Back;
    case BSP_INPUT_NAVIGATION_KEY_F2:return Key::Previous;
    case BSP_INPUT_NAVIGATION_KEY_F3:return Key::Next;
    case BSP_INPUT_NAVIGATION_KEY_F4:return Key::Filters;
    case BSP_INPUT_NAVIGATION_KEY_F5:return Key::Settings;
    case BSP_INPUT_NAVIGATION_KEY_F6:case BSP_INPUT_NAVIGATION_KEY_MENU:return Key::Menu;
    case BSP_INPUT_NAVIGATION_KEY_UP:return Key::Up;
    case BSP_INPUT_NAVIGATION_KEY_DOWN:return Key::Down;
    case BSP_INPUT_NAVIGATION_KEY_LEFT:return Key::Left;
    case BSP_INPUT_NAVIGATION_KEY_RIGHT:return Key::Right;
    case BSP_INPUT_NAVIGATION_KEY_RETURN:return Key::Enter;
    case BSP_INPUT_NAVIGATION_KEY_TAB:return Key::Tab;
    case BSP_INPUT_NAVIGATION_KEY_HOME:return Key::Home;
    case BSP_INPUT_NAVIGATION_KEY_END:return Key::End;
    case BSP_INPUT_NAVIGATION_KEY_PGUP:return Key::PageUp;
    case BSP_INPUT_NAVIGATION_KEY_PGDN:return Key::PageDown;
    default:return Key::None;
    }
}
void scan_alerts() {
    for(unsigned i=0;i<snapshot->count;i++) {
        const auto &a=snapshot->aircraft[i];if(a.stale_ms)continue;
        unsigned flags=(a.military?1u:0u)|(a.emergency?2u:0u),index=0;
        for(;index<alert_seen_count;index++)if(!strcmp(alert_seen[index].hex,a.hex))break;
        if(index==alert_seen_count) {
            if(alert_seen_count==2000)continue;
            snprintf(alert_seen[index].hex,7,"%s",a.hex);alert_seen_count++;
        }
        unsigned fresh=flags&~alert_seen[index].flags;alert_seen[index].flags=flags;
        bool emergency=(fresh&2)&&nav.settings.preferences.alert_emergency;
        bool military=(fresh&1)&&nav.settings.preferences.alert_military;
        if(!emergency && !military)continue;
        if(alert_count==32){memmove(alerts,alerts+1,31*sizeof(Alert));alert_count--;}
        auto &alert=alerts[alert_count++];alert={};snprintf(alert.hex,7,"%s",a.hex);alert.at=now_ms();alert.id=next_alert_id++;
        snprintf(alert.title,sizeof(alert.title),"%s: %s%s",emergency?"Emergency":"Military",flight(a),emergency?format(" squawk %04d",a.squawk).c_str():" (derived)");
        snprintf(toast_text,sizeof(toast_text),"%s | F6 > Alerts to open or dismiss",alert.title);toast_until=now_ms()+10000;
        // Never change focus or selection while a user is editing, inspecting or selecting.
        if(nav.settings.preferences.alert_autofocus && nav.modal==Modal::None && !*nav.selected && !nav.selection_lost) {
            nav.direct_view(0);nav.filter=0;nav.query[0]=0;nav.centered=false;snprintf(nav.tracking,7,"%s",a.hex);snprintf(nav.selected,7,"%s",a.hex);nav.activity(now_ms());
        }
    }
}
const lv_image_dsc_t *map_image=nullptr;
void draw_space(lv_event_t *e) {
    if(map_image) {
        lv_area_t area;lv_obj_get_coords(airspace,&area);
        lv_draw_image_dsc_t d;lv_draw_image_dsc_init(&d);d.src=map_image;
        lv_draw_image(lv_event_get_layer(e),&d,&area);
    }
    draw_airspace(e,map_draw_nav,*snapshot,now_ms());
    if(map_image) {
        lv_draw_label_dsc_t d;lv_draw_label_dsc_init(&d);d.text=adsb_maps::attribution();d.text_local=true;
        d.font=&lv_font_montserrat_14;d.color=lv_color_hex(0xffffff);
        lv_area_t area={8,Layout::content_y+328,791,Layout::content_y+365};lv_draw_label(lv_event_get_layer(e),&d,&area);
    }
}
std::string details(const adsb_aircraft_t &a) {
    auto r=route_for(a);const adsb_enrichment_t *e=r?&r->data:nullptr;
    bool metric=nav.settings.preferences.metric;
    return format("%s / ICAO %s\n\nIDENTITY\nOperator: %s\nRegistration: %s\nType: %s | Category: %s\nDescription: %s\n\nROUTE\n%s > %s\n\nFLIGHT\nStatus: %s | Squawk: %04d\nAltitude: %.0f %s\nSpeed: %.0f %s | Heading: %03d deg\nVertical rate: %+.0f %s\nIAS: %.0f %s | TAS: %.0f %s\nMach: %.3f\n",
        flight(a),a.hex,known(e&&*e->operator_name?e->operator_name:a.operator_name),known(e&&*e->registration?e->registration:a.registration),known(a.type),known(a.category),known(e&&*e->type?e->type:a.description),
        e?known(e->origin):"Unavailable",e?known(e->destination):"Unavailable",flight_status(a),a.squawk,
        a.altitude*(metric?.3048:1),metric?"m":"ft",a.speed*(metric?1.852:1),metric?"km/h":"kt",a.heading,
        a.vertical_rate*(metric?.3048:1),metric?"m/min":"ft/min",a.ias*(metric?1.852:1),metric?"km/h":"kt",a.tas*(metric?1.852:1),metric?"km/h":"kt",(double)a.mach)
        +format("Roll: %.1f deg | QNH: %.1f hPa\nSelected altitude: %.0f %s\n\nPOSITION\nLatitude: %.5f\nLongitude: %.5f\nDistance from home: %.1f %s\nPosition age: %llus%s\nMilitary classification: %s\n\nPHOTO CREDIT\n%s\n\n%s\nMissing numeric feed values are shown as 0.\n",
        (double)a.roll,(double)a.nav_qnh,a.nav_altitude*(metric?.3048:1),metric?"m":"ft",a.lat,a.lon,distance(a.lat,a.lon,nav.settings.lat,nav.settings.lon)*(metric?1.852:1),metric?"km":"nm",
        (unsigned long long)((now_ms()-std::min(now_ms(),a.seen_ms))/1000),a.stale_ms?" (stale)":"",a.military?"Derived from ICAO range":"Not identified",e?known(e->credit):"Unavailable",enrichment);
}
unsigned object_count(lv_obj_t *o) {unsigned count=1;for(unsigned i=0;i<lv_obj_get_child_count(o);i++)count+=object_count(lv_obj_get_child(o,i));return count;}
std::string statistics() {
    unsigned cats[5]={},alt[6]={},speed[6]={};
    const adsb_aircraft_t *fast=nullptr,*slow=nullptr,*high=nullptr,*low=nullptr,*close=nullptr;
    double nearest=1e9;
    for(unsigned i=0;i<snapshot->count;i++) {
        const auto &a=snapshot->aircraft[i];if(!passes(a,nav.filter) || !(contains(a.callsign,nav.query)||contains(a.registration,nav.query)||contains(a.hex,nav.query)))continue;
        cats[helicopter(a)?2:passes(a,1)?0:1]++;if(a.military)cats[3]++;if(a.emergency)cats[4]++;
        alt[a.ground?0:a.altitude<5000?1:a.altitude<15000?2:a.altitude<25000?3:a.altitude<35000?4:5]++;
        speed[a.ground?0:a.speed<200?1:a.speed<300?2:a.speed<400?3:a.speed<500?4:5]++;
        if(a.speed>0 && (!fast||a.speed>fast->speed))fast=&a;
        if(!a.ground && a.speed>30 && (!slow||a.speed<slow->speed))slow=&a;
        if(!a.ground && a.altitude>100) {if(!high||a.altitude>high->altitude)high=&a;if(!low||a.altitude<low->altitude)low=&a;}
        double d=distance(a.lat,a.lon,nav.settings.lat,nav.settings.lon);if(d<nearest){nearest=d;close=&a;}
    }
    auto record=[&](const adsb_aircraft_t *a,int value,bool velocity) {
        return a?format("%s  %.0f %s",flight(*a),value*(nav.settings.preferences.metric?(velocity?1.852:.3048):1),nav.settings.preferences.metric?(velocity?"km/h":"m"):(velocity?"kt":"ft")):std::string("Unavailable");
    };
    auto out=format("TRAFFIC - active filter and search\nJets %u    GA %u    Helicopters %u    Military %u    Emergency %u\nAltitude (ft): Ground %u | <5k %u | <15k %u | <25k %u | <35k %u | 35k+ %u\nSpeed (kt): Ground %u | <200 %u | <300 %u | <400 %u | <500 %u | 500+ %u\n\nCURRENT RECORDS - active filter and search\nFastest: %s\nSlowest airborne (>30 kt): %s\nHighest: %s\nLowest airborne (>100 ft): %s\nClosest: %s\n\n",
        cats[0],cats[1],cats[2],cats[3],cats[4],alt[0],alt[1],alt[2],alt[3],alt[4],alt[5],speed[0],speed[1],speed[2],speed[3],speed[4],speed[5],
        record(fast,fast?fast->speed:0,true).c_str(),record(slow,slow?slow->speed:0,true).c_str(),record(high,high?high->altitude:0,false).c_str(),record(low,low?low->altitude:0,false).c_str(),
        close?format("%s %.1f %s",flight(*close),nearest*(nav.settings.preferences.metric?1.852:1),nav.settings.preferences.metric?"km":"nm").c_str():"Unavailable");
    out+=format("SESSION - all received aircraft\nUnique ICAO: %u%s | Peak aircraft: %u | Uptime: %llus\nTop airlines (unique aircraft): %s\nTop types (unique aircraft): %s\nRanking capacity: 100 types / 100 airlines\n\n",session.unique,session.unique==2000?" (capacity reached)":"",session.peak,(unsigned long long)(now_ms()/1000),session.top(session.airlines).c_str(),session.top(session.types).c_str());
    out+=format("SYSTEM\nInternal heap: %zu KiB | Minimum: %zu KiB | Largest block: %zu KiB\nPSRAM free: %zu KiB | Flash size: %u MiB\nTasks: %u | UI stack minimum free: %u bytes\nLVGL objects: %u | Temperature: unavailable | FPS: unavailable\nBattery: %s\n\n",health.internal_free/1024,health.internal_min/1024,health.largest_block/1024,health.psram_free/1024,health.flash_bytes/1048576,health.tasks,health.stack_free,object_count(lv_screen_active()),health.battery_percent>=0?format("%d%% | %d mV%s",health.battery_percent,health.battery_mv,health.charging?" | charging":"").c_str():"unavailable");
    out+=format("NETWORK\n%s\nIP: %s | RSSI: %s\nFetch: %u OK / %u failed | Enrichment HTTP: %u OK / %u failed\nReceived: %llu KiB | Last fetch: %llu ms\nPolling: %us | API coverage: %u nm\nData age: %s\nErrors retained: %u / 16 | F6 > Error history / Clear error history\n",network,*health.ip?health.ip:"Unavailable",health.connected?format("%d dBm",health.rssi).c_str():"Unavailable",diagnostics.fetch_ok,diagnostics.fetch_fail,diagnostics.enrich_ok,diagnostics.enrich_fail,(unsigned long long)(diagnostics.bytes_received/1024),(unsigned long long)diagnostics.last_fetch_ms,nav.settings.poll_seconds,nav.settings.radius_nm,snapshot->updated_ms?format("%llus",(unsigned long long)((now_ms()-snapshot->updated_ms)/1000)).c_str():"waiting for first response",diagnostics.error_count);
    out+=format("Active polling: %us (includes server backoff)\n",std::max(nav.settings.poll_seconds,diagnostics.effective_poll_seconds));
    return out;
}
void refresh() {
    nav.rebuild(*snapshot);
    adsb_maps::Request displayed{nav.center_lat,nav.center_lon,nav.range()};
    map_image=adsb_maps::image(displayed,nav.view==View::Map,&displayed,*nav.tracking!=0);
    map_draw_nav=nav;
    if(map_image) {
        map_draw_nav.centered=false;map_draw_nav.tracking[0]=0;
        map_draw_nav.center_lat=displayed.lat;map_draw_nav.center_lon=displayed.lon;
        map_draw_nav.rebuild(*snapshot);
    }
    bool space=nav.view==View::Map||nav.view==View::Radar,radar=nav.view==View::Radar,arrivals=nav.view==View::Arrivals;
    auto hide=[](lv_obj_t *o,bool hidden){if(lv_obj_has_flag(o,LV_OBJ_FLAG_HIDDEN)==hidden)return;if(hidden)lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(o,LV_OBJ_FLAG_HIDDEN);};
    char text[512];uint64_t age=snapshot->updated_ms?(now_ms()-snapshot->updated_ms)/1000:0;
    snprintf(text,sizeof(text),"%s | %s | %u aircraft | %s | %s | %s",view_names[(unsigned)nav.view],(nav.settings.feed_provider==1?"https://adsb.fi":"https://adsb.lol"),snapshot->count,health.connected?"Wi-Fi":"Offline",snapshot->updated_ms?format("age %llus",(unsigned long long)age).c_str():"Loading",health.battery_percent>=0?format("%d%%%s",health.battery_percent,health.charging?" charging":"").c_str():"Battery N/A");
    set_label(heading,text);
    hide(airspace,nav.view!=View::Map);hide(radar_base,true);hide(radar_motion,!radar);hide(board_header,!arrivals);
    if(radar && nav.modal==Modal::None) {
        static bool previous_motion=true;
        bool motion_changed=previous_motion!=nav.settings.preferences.reduce_motion;
        previous_motion=nav.settings.preferences.reduce_motion;
        if(radar_scene.update(radar_base,nav,*snapshot,now_ms()))
            radar_scene.sync(radar_motion,radar_base,now_ms(),!previous_motion);
        else if(motion_changed)radar_scene.animate(radar_motion,now_ms(),!previous_motion);
    }
    hide(stats_panel,nav.view!=View::Stats);
    if(space && !radar && nav.modal==Modal::None)lv_obj_invalidate(airspace);
    lv_obj_set_y(summary,8);lv_obj_set_size(summary,space?(radar?180:330):776,space?(radar?270:180):32);
    if(lv_obj_get_style_bg_opa(summary,0)!=(space?LV_OPA_90:LV_OPA_TRANSP))lv_obj_set_style_bg_opa(summary,space?LV_OPA_90:LV_OPA_TRANSP,0);
    if(lv_label_get_long_mode(summary)!=LV_LABEL_LONG_WRAP)lv_label_set_long_mode(summary,LV_LABEL_LONG_WRAP);
    auto selected=nav.find(*snapshot,nav.selected);
    std::string info=format("%s | %u visible",filter_names[nav.filter],nav.count);
    if(space) {
        info+=format("\nRange %u nm | API %u nm%s%s",nav.range(),nav.settings.radius_nm,nav.range()>nav.settings.radius_nm?"\nBeyond API coverage":"",*nav.tracking?"\nTracking aircraft":"");
        if(selected)info+=format("\n%s / %s\n%.0f %s | %.0f %s\n%s",flight(*selected),selected->hex,selected->altitude*(nav.settings.preferences.metric?.3048:1),nav.settings.preferences.metric?"m":"ft",selected->speed*(nav.settings.preferences.metric?1.852:1),nav.settings.preferences.metric?"km/h":"kt",flight_status(*selected));
        else info+="\nUp/Down: select | Enter: details";
        if(!radar) {
            info+=*nav.settings.city_name?format("\nHome: %s, %s",nav.settings.city_name,nav.settings.city_country):format("\nHome: %.4f, %.4f",nav.settings.lat,nav.settings.lon);
            info+=format("\n%s",adsb_maps::status());
        }
    } else if(arrivals)info+=format(" | Page %u/2 | %s | %s",nav.page+1,nav.sort==Sort::Distance?"Distance":nav.sort==Sort::Callsign?"Callsign":nav.sort==Sort::Altitude?"Altitude":"Speed",nav.descending?"Descending":"Ascending");
    else info="Stats | Up / Down scroll | F6 error history and alerts";
    int summary_x=12;if(space && !radar && selected) {auto p=project(selected->lat,selected->lon,nav);summary_x=p.x<400?458:12;}lv_obj_set_x(summary,summary_x);
    lv_point_t summary_size;lv_text_get_size(&summary_size,info.c_str(),LV_FONT_DEFAULT,0,0,space?(radar?180:330):776,LV_TEXT_FLAG_NONE);
    set_font(summary,space && summary_size.y>(radar?270:180)?&lv_font_montserrat_14:LV_FONT_DEFAULT);
    set_label(summary,info.c_str());
    int selected_row=nav.selected_row(*snapshot);unsigned start=selected_row<0?0:((unsigned)selected_row/Layout::rows)*Layout::rows;
    bool metric_headers=nav.settings.preferences.metric;
    const char *primary[]={"FLIGHT","ROUTE","TYPE",metric_headers?"ALT m":"ALT ft",metric_headers?"km/h":"kt",metric_headers?"DIST km":"DIST nm","STATUS"};
    const char *extended[]={"FLIGHT","ICAO","TYPE","HDG",metric_headers?"m/min":"ft/min","SQUAWK","CATEGORY"};
    for(unsigned c=0;c<7;c++)set_label(column_titles[c],nav.page?extended[c]:primary[c]);

    for(unsigned i=0;i<Layout::rows;i++) {
        hide(rows[i],!arrivals || start+i>=nav.count);
        if(!arrivals || start+i>=nav.count)continue;
        auto &a=snapshot->aircraft[nav.visible[start+i]];auto r=route_for(a);bool metric=nav.settings.preferences.metric;
        std::string values[7]={flight(a),r&&*r->data.origin&&*r->data.destination?format("%.3s > %.3s",r->data.origin,r->data.destination):"-- > --",*a.type?a.type:"--",format("%.0f",a.altitude*(metric?.3048:1)),format("%.0f",a.speed*(metric?1.852:1)),format("%.1f",distance(a.lat,a.lon,nav.settings.lat,nav.settings.lon)*(metric?1.852:1)),flight_status(a)};
        if(nav.page) {values[1]=a.hex;values[3]=format("%03d deg",a.heading);values[4]=format("%+.0f",a.vertical_rate*(metric?.3048:1));values[5]=format("%04d",a.squawk);values[6]=format("%s%s",*a.category?a.category:"--",a.military?" MIL":"");}
        for(unsigned c=0;c<7;c++) {
            lv_point_t extent;lv_text_get_size(&extent,values[c].c_str(),LV_FONT_DEFAULT,0,0,10000,LV_TEXT_FLAG_NONE);
            set_font(cells[i][c],extent.x>column_widths[c]-8?&lv_font_montserrat_14:LV_FONT_DEFAULT);
            if(strcmp(lv_label_get_text(cells[i][c]),values[c].c_str())) {
                set_label(cells[i][c],values[c].c_str());
                if(!nav.settings.preferences.reduce_motion && nav.modal==Modal::None)lv_obj_fade_in(cells[i][c],160,0);
            }
            set_color(cells[i][c],(int)(start+i)==selected_row?0x55ddff:a.emergency?0xff7777:0xe5edf4);
        }
    }
    if(nav.view==View::Stats) {
        auto key=format("%llu/%u/%s/%u/%s",(unsigned long long)snapshot->updated_ms,nav.filter,nav.query,nav.settings.preferences.metric,network);
        if(now_ms()>=stats_due || key!=stats_key) {stats_due=now_ms()+1000;stats_key=key;stats_builds++;stats_report=statistics();size_t begin=0;
            for(unsigned i=0;i<5;i++) {size_t end=stats_report.find("\n\n",begin);set_label(stats_sections[i],stats_report.substr(begin,end==std::string::npos?end:end-begin).c_str());begin=end==std::string::npos?stats_report.size():end+2;}
        }
    }
    if(nav.modal==Modal::Detail && detail_text) {
        auto a=nav.find(*snapshot,nav.detail);auto report=a?details(*a):format("Aircraft %s is no longer available.\n\n%s",nav.detail,enrichment);
        auto split=report.find("\nFLIGHT\n");
        if(split!=std::string::npos) {set_label(detail_text,report.substr(0,split).c_str());set_label(detail_right,report.substr(split+1).c_str());}
        else {set_label(detail_text,report.c_str());set_label(detail_right,"");}
        lv_obj_update_layout(panel);
        int32_t end=std::max<int32_t>(0,lv_obj_get_scroll_top(panel)+lv_obj_get_scroll_bottom(panel));
        lv_obj_scroll_to_y(panel,std::max<int32_t>(0,std::min(end,lv_obj_get_scroll_y(panel))),LV_ANIM_OFF);
    }
    if(now_ms()>=feedback_until)feedback[0]=0;
    if(modal_notice)set_label(modal_notice,feedback);
    lv_obj_set_pos(notice,12,radar?290:370);lv_obj_set_size(notice,radar?180:776,radar?120:44);if(lv_label_get_long_mode(notice)!=LV_LABEL_LONG_WRAP)lv_label_set_long_mode(notice,LV_LABEL_LONG_WRAP);
    std::string status;
    if(*feedback)status=feedback;
    else if(nav.selection_lost)status="Selection left this view. Use arrows to choose again.";
    else if(!ready)status="Services unavailable. Return to launcher; shared settings preserved.";
    else {
        char service[160];adsb_service_status(service,sizeof(service));
        if(!snapshot->updated_ms)status=radar?format("Loading aircraft\nWi-Fi %s\n4: network status\nF6: reconnect/errors",health.connected?"connected":"offline"):format("Loading aircraft | %s | %s",network,service);
        else if(!nav.count && nav.view!=View::Stats)status=format("%s | %s",snapshot->count?"No aircraft match filter, range and search":"No aircraft reported by service",service);
        else if(nav.view==View::Stats)status=format("%s | F6: error history / alerts",service);
        else status=format("%s%s | %s",age>std::max(nav.settings.poll_seconds,diagnostics.effective_poll_seconds)*3?"STALE DATA | ":"",service,space?"F6 actions / alerts":format("Rows %u-%u of %u",nav.count?start+1:0,std::min(start+10,nav.count),nav.count).c_str());
    }
    set_label(notice,status.c_str());
    hide(toast,now_ms()>=toast_until);if(now_ms()<toast_until) {
        set_label(toast,toast_text);lv_obj_move_to_index(toast,-1);
        lv_obj_set_y(toast,nav.modal==Modal::None?32:392); // Keep focused form fields visible.
    }
    set_label(footer,(nav.modal==Modal::Detail || nav.modal==Modal::Help)?"F1 Back   Up / Down Scroll   Tab Next control   Enter Activate":nav.modal!=Modal::None?"F1 Back / Cancel   Tab Next   Shift+Tab Previous   Enter Activate":"F1 Back   F2 Prev   F3 Next   F4 Filters   F5 Settings   F6 Menu");
}
}
extern "C" void adsb_ui_init(bool services_ready) {
    ready=services_ready;
    snapshot=(adsb_snapshot_t*)heap_caps_calloc(1,sizeof(*snapshot),MALLOC_CAP_SPIRAM);
    routes=(Route*)heap_caps_calloc(200,sizeof(Route),MALLOC_CAP_SPIRAM);
    if(!snapshot || !routes)abort();
    group=lv_group_create();lv_group_set_wrap(group,true);
    auto screen=lv_screen_active();plain(screen);
    lv_obj_set_style_bg_color(screen,lv_color_hex(0x101820),0);lv_obj_set_style_text_color(screen,lv_color_hex(0xe5edf4),0);
    heading=label(screen,"");lv_obj_set_pos(heading,8,4);lv_obj_set_size(heading,784,24);lv_label_set_long_mode(heading,LV_LABEL_LONG_CLIP);
    content=lv_obj_create(screen);plain(content);lv_obj_set_pos(content,0,Layout::content_y);lv_obj_set_size(content,800,418);
    lv_obj_set_style_bg_opa(content,LV_OPA_TRANSP,0);
    summary=label(content,"");lv_obj_set_style_bg_color(summary,lv_color_hex(0x101820),0);lv_obj_set_pos(summary,12,8);lv_obj_set_size(summary,776,58);lv_label_set_long_mode(summary,LV_LABEL_LONG_CLIP);
    airspace=lv_obj_create(content);plain(airspace);lv_obj_set_pos(airspace,0,0);lv_obj_set_size(airspace,800,418);
    lv_obj_set_style_bg_color(airspace,lv_color_hex(0x0b1821),0);lv_obj_add_event_cb(airspace,draw_space,LV_EVENT_DRAW_MAIN,nullptr);
    lv_obj_move_to_index(airspace,0);
    radar_base=lv_canvas_create(content);plain(radar_base);lv_obj_set_pos(radar_base,200,0);
    void *radar_pixels=heap_caps_calloc(400*418,2,MALLOC_CAP_SPIRAM);
    if(!radar_pixels)abort();
    lv_canvas_set_buffer(radar_base,radar_pixels,400,418,LV_COLOR_FORMAT_RGB565);
    lv_obj_move_to_index(radar_base,1);
    radar_motion=lv_canvas_create(content);plain(radar_motion);lv_obj_set_pos(radar_motion,200,0);
    void *sweep_pixels=heap_caps_calloc(400*418,2,MALLOC_CAP_SPIRAM);
    if(!sweep_pixels)abort();
    lv_canvas_set_buffer(radar_motion,sweep_pixels,400,418,LV_COLOR_FORMAT_RGB565);
    lv_obj_move_to_index(radar_motion,2);
    board_header=lv_obj_create(content);plain(board_header);lv_obj_set_pos(board_header,12,44);lv_obj_set_size(board_header,776,28);lv_obj_set_style_bg_opa(board_header,LV_OPA_TRANSP,0);
    for(unsigned i=0;i<Layout::rows;i++) {
        rows[i]=lv_obj_create(content);plain(rows[i]);lv_obj_set_pos(rows[i],12,76+i*Layout::row_height);lv_obj_set_size(rows[i],776,28);lv_obj_set_style_bg_opa(rows[i],LV_OPA_TRANSP,0);
        int x=0;
        for(unsigned c=0;c<7;c++) {
            if(i==0){column_titles[c]=label(board_header,"");lv_obj_set_pos(column_titles[c],x+3,3);lv_obj_set_size(column_titles[c],column_widths[c]-6,24);}
            cells[i][c]=label(rows[i],"");lv_obj_set_pos(cells[i][c],x+1,1);lv_obj_set_size(cells[i][c],column_widths[c]-2,26);
            lv_label_set_long_mode(cells[i][c],LV_LABEL_LONG_CLIP);lv_obj_set_style_bg_opa(cells[i][c],LV_OPA_COVER,0);
            lv_obj_set_style_bg_color(cells[i][c],lv_color_hex(0x202c39),0);lv_obj_set_style_border_width(cells[i][c],1,0);lv_obj_set_style_border_color(cells[i][c],lv_color_hex(0x35485b),0);lv_obj_set_style_pad_left(cells[i][c],3,0);x+=column_widths[c];
        }
    }
    stats_panel=lv_obj_create(content);plain(stats_panel);lv_obj_set_pos(stats_panel,12,48);lv_obj_set_size(stats_panel,776,312);lv_obj_add_flag(stats_panel,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(stats_panel,LV_DIR_VER);lv_obj_remove_flag(stats_panel,LV_OBJ_FLAG_SCROLL_ELASTIC);lv_obj_set_style_bg_opa(stats_panel,LV_OPA_TRANSP,0);lv_obj_set_flex_flow(stats_panel,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_row(stats_panel,18,0);
    for(auto &section:stats_sections)section=label(stats_panel,"");
    stats_text=stats_sections[0];
    notice=label(content,"");lv_obj_set_pos(notice,12,370);lv_obj_set_size(notice,776,44);lv_label_set_long_mode(notice,LV_LABEL_LONG_CLIP);
    footer=label(screen,"");lv_obj_set_pos(footer,12,452);lv_obj_set_size(footer,776,26);lv_label_set_long_mode(footer,LV_LABEL_LONG_CLIP);
    toast=label(screen,"");lv_obj_set_pos(toast,12,32);lv_obj_set_size(toast,776,52);lv_obj_set_style_bg_color(toast,lv_color_hex(0x4c331a),0);lv_obj_set_style_bg_opa(toast,LV_OPA_COVER,0);
    if(!adsb_settings_get(&nav.settings)) {nav.settings.radius_nm=150;nav.settings.poll_seconds=5;nav.settings.preferences.trail_points=30;nav.settings.preferences.cycle_seconds=15;nav.settings.preferences.cycle_idle_seconds=60;}
    nav.recenter();nav.activity(now_ms());
    if(!nav.settings.configured && ready)open(Modal::Settings);
    refresh();
}
extern "C" void adsb_ui_input(const bsp_input_event_t *event) {
    if(event->type==INPUT_EVENT_TYPE_NAVIGATION) {
        auto &n=event->args_navigation;Key key=translate(n.key);
        if(gate.edge(key,n.state,now_ms()))dispatch(key,n.modifiers&BSP_INPUT_MODIFIER_SHIFT);
    } else if(event->type==INPUT_EVENT_TYPE_KEYBOARD) {
        auto &t=event->args_keyboard;
        nav.activity(now_ms());dirty=true;
        if(editor()) {
            if(t.ascii==8 || t.ascii==127)lv_textarea_delete_char(lv_group_get_focused(group));
            else if((unsigned char)t.utf8[0]>=32 && t.utf8[0]!=127) {char utf8[8]={};memcpy(utf8,t.utf8,7);lv_textarea_add_text(lv_group_get_focused(group),utf8);}
            if(nav.modal==Modal::Cities) {
                snprintf(city_query,sizeof(city_query),"%s",lv_textarea_get_text(fields[0]));
                snprintf(city_country_filter,sizeof(city_country_filter),"%.2s",lv_textarea_get_text(fields[1]));
                for(char &c:city_country_filter)c=std::toupper((unsigned char)c);
                city_matches={};++city_token;city_pending=strlen(city_query)>=2;city_due=now_ms()+200;
                int32_t cursor=lv_textarea_get_cursor_pos(lv_group_get_focused(group));
                remember_focus();render_modal();lv_textarea_set_cursor_pos(lv_group_get_focused(group),cursor);
            }
            return;
        }
        if(nav.modal!=Modal::None || (t.modifiers&(BSP_INPUT_MODIFIER_CTRL|BSP_INPUT_MODIFIER_ALT|BSP_INPUT_MODIFIER_SUPER)))return;
        if(t.ascii>='1' && t.ascii<='4'){nav.direct_view(t.ascii-'1');render_modal();}
        if(t.ascii=='+' || t.ascii=='='){nav.step_range(1);preferences_dirty=true;}
        if(t.ascii=='-'){nav.step_range(-1);preferences_dirty=true;}
        if(t.ascii=='h' || t.ascii=='H')nav.recenter();
        if(t.ascii=='/')open(Modal::Search);
    }
    // Raw scancodes duplicate the two semantic streams; actions stay with BSP.
}
extern "C" void adsb_ui_tick(void) {
    uint64_t now=now_ms();auto key=gate.tick(now);if(key!=Key::None)dispatch(key);
    if(nav.tick(now))dirty=true;
    if(nav.modal==Modal::Cities && city_pending && city_due && now>=city_due) {
        city_due=0;
        if(!adsb_cities::request(city_query,city_country_filter,city_token)){city_pending=false;city_matches={};remember_focus();render_modal();dirty=true;}
    }
    adsb_cities::Results found;
    if(adsb_cities::receive(found) && nav.modal==Modal::Cities && found.token==city_token) {
        city_matches=found;city_pending=false;remember_focus();render_modal();dirty=true;
    }
    // Coalesce explicit preference changes. Automatic view cycling does not write NVS.
    if(preferences_dirty && ready && nav.settings.configured && nav.modal!=Modal::Settings && nav.modal!=Modal::Cities && now-nav.last_action>=1000) {
        preferences_dirty=false;
        if(!adsb_settings_save(&nav.settings))message("Preference save failed; retry in Settings");
    }
    if(now>=health_due) {health_due=now+5000;adsb_health_read(&health);if(ready)adsb_diagnostics_copy(&diagnostics);dirty=true;}
    if(nav.modal==Modal::None && nav.view==View::Radar && !nav.settings.preferences.reduce_motion && now>=animation_due) {animation_due=now+40;radar_scene.animate(radar_motion,now,true);}
    if(now>=next_refresh) {
        next_refresh=now+500;if(ready)adsb_snapshot_copy(snapshot);
        session.update(*snapshot);scan_alerts();
        adsb_enrichment_t e;
        if(adsb_enrich_receive(&e)) {
            const auto a=nav.find(*snapshot,e.hex);
            if(a && !strcmp(a->callsign,e.callsign) && (e.token==ADSB_BACKGROUND_ROUTE_TOKEN || nav.accepts(e))) {
                auto r=route_for(*a);if(!r)r=&routes[route_slot++%200];r->data=e;r->requested=now;
            }
            if(nav.accepts(e))snprintf(enrichment,sizeof(enrichment),"%s",e.valid?"Enrichment received. Unavailable fields were not supplied by the services.":"Enrichment unavailable; reopen to retry.");
        }
        // Gradually fill Arrivals routes with bounded requests; foreground details use independent tokens.
        if(ready && nav.view==View::Arrivals && nav.modal==Modal::None && now>=next_route) {
            nav.rebuild(*snapshot);
            next_route=now+10000;
            int selected=nav.selected_row(*snapshot);unsigned start=selected<0?0:((unsigned)selected/10)*10;
            for(unsigned i=0;i<nav.count;i++) {
                const auto &a=snapshot->aircraft[nav.visible[(start+i)%nav.count]];if(!*a.callsign)continue;
                auto r=route_for(a);if(r && now-r->requested<(r->data.valid?3600000u:60000u))continue;
                if(adsb_enrich_request(a.hex,a.callsign,ADSB_BACKGROUND_ROUTE_TOKEN)) {
                    if(!r)r=&routes[route_slot++%200];
                    *r={};snprintf(r->data.hex,7,"%s",a.hex);snprintf(r->data.callsign,9,"%s",a.callsign);r->requested=now;
                }
                break;
            }
        }
        dirty=true;
    }
    if(dirty){refresh();dirty=false;}
}
extern "C" void adsb_ui_network(const char *text){snprintf(network,sizeof(network),"%s",text);dirty=true;}
extern "C" bool adsb_ui_take_reconnect(void){bool value=reconnect_requested;reconnect_requested=false;return value;}
extern "C" bool adsb_ui_take_exit(void){return exit_requested;}
