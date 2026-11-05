#pragma once
#include "navigation.h"
#include "lvgl.h"
#include <string>
#include <cstdarg>

namespace adsb_ui {
inline std::string format(const char *f,...) {
    va_list ap;va_start(ap,f);va_list copy;va_copy(copy,ap);
    int count=vsnprintf(nullptr,0,f,copy);va_end(copy);
    if(count<0){va_end(ap);return {};}
    std::string out((size_t)count+1,'\0');vsnprintf(&out[0],out.size(),f,ap);va_end(ap);out.resize(count);return out;
}
inline const char *flight(const adsb_aircraft_t &a) {return *a.callsign?a.callsign:a.hex;}
inline const char *known(const char *s) {return *s?s:"Unavailable";}
inline const char *flight_status(const adsb_aircraft_t &a) {
    return a.emergency?"EMERGENCY":a.stale_ms?"STALE":a.ground?"GROUND":a.vertical_rate>200?"CLIMB":a.vertical_rate<-200?"DESCEND":"LEVEL";
}
inline uint32_t altitude_color(int altitude) {
    return altitude<5000?0x55dd99:altitude<15000?0xaadd55:altitude<25000?0xeedd55:altitude<35000?0xffaa55:0xdd88ff;
}
using Position=adsb_geo::Point;
struct RadarProjection:adsb_geo::Projection {
    explicit RadarProjection(const Navigation &n):Projection(n.center_lat,n.center_lon,n.range(),199){}
};
inline Position project(double lat,double lon,const Navigation &n) {
    return adsb_geo::Projection(n.center_lat,n.center_lon,n.range(),n.view==View::Radar?199:400)(lat,lon);
}
inline bool on_canvas(Position p,bool radar) {
    return p.x>=0 && p.x<800 && p.y>=0 && p.y<418 && (!radar || hypot(p.x-400,p.y-209)<=199);
}
inline void line(lv_layer_t *layer,double x1,double y1,double x2,double y2,uint32_t color,unsigned opacity=255,int width=1) {
    lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.p1={(lv_value_precise_t)x1,(lv_value_precise_t)y1};d.p2={(lv_value_precise_t)x2,(lv_value_precise_t)y2};
    d.color=lv_color_hex(color);d.opa=opacity;d.width=width;lv_draw_line(layer,&d);
}
inline void ring(lv_layer_t *layer,int x,int y,unsigned radius,uint32_t color,unsigned opacity=255,int width=1) {
    lv_draw_arc_dsc_t d;lv_draw_arc_dsc_init(&d);d.center={x,y};d.radius=radius;d.start_angle=0;d.end_angle=360;d.width=width;d.color=lv_color_hex(color);d.opa=opacity;lv_draw_arc(layer,&d);
}
inline void draw_text(lv_layer_t *layer,int x,int y,int width,const char *text,uint32_t color=0xc6d8db) {
    lv_draw_label_dsc_t d;lv_draw_label_dsc_init(&d);d.text=text;d.text_local=true;d.color=lv_color_hex(color);d.font=LV_FONT_DEFAULT;
    lv_area_t a={x,y,x+width-1,y+24};lv_draw_label(layer,&d,&a);
}
// Direct drawing avoids an extra full-screen pixel buffer and clips at the content object.
inline void draw_airspace(lv_event_t *event,const Navigation &n,const adsb_snapshot_t &s,uint64_t now) {
    auto layer=lv_event_get_layer(event);lv_area_t area;lv_obj_get_coords((lv_obj_t*)lv_event_get_target(event),&area);
    int oy=area.y1,ox=area.x1;bool radar=n.view==View::Radar;
    adsb_geo::Projection projection(n.center_lat,n.center_lon,n.range(),radar?199:400);
    if(radar) {
        for(unsigned r=1;r<=4;r++)ring(layer,ox+400,oy+209,199*r/4,0x20563f);
        line(layer,ox+201,oy+209,ox+599,oy+209,0x20563f);line(layer,ox+400,oy+10,ox+400,oy+408,0x20563f);
        draw_text(layer,ox+394,oy+10,25,"N",0x70c99c);
        for(unsigned r=1;r<4;r++)draw_text(layer,ox+404,oy+209-199*r/4,90,format("%g",n.range()*r/4.).c_str(),0x70c99c);
        if(!n.settings.preferences.reduce_motion) {
            double angle=(now%6000)/6000.*6.28318530718;
            for(int i=12;i>=0;i--) {
                double a=angle-i*.025;line(layer,ox+400,oy+209,ox+400+199*sin(a),oy+209-199*cos(a),0x44cc88,(13-i)*8);
            }
        }
    } else {
        for(int x=0;x<800;x+=100)line(layer,ox+x,oy,ox+x,oy+417,0x1b3446);
        for(int y=9;y<418;y+=100)line(layer,ox,oy+y,ox+799,oy+y,0x1b3446);
        draw_text(layer,ox+770,oy+6,25,"N");
    }
    Position home=projection(n.settings.lat,n.settings.lon);
    if(on_canvas(home,radar)) {line(layer,ox+home.x-6,oy+home.y,ox+home.x+6,oy+home.y,0xffffff);line(layer,ox+home.x,oy+home.y-6,ox+home.x,oy+home.y+6,0xffffff);}
    for(unsigned i=0;i<n.count;i++) {
        const auto &a=s.aircraft[n.visible[i]];auto p=projection(a.lat,a.lon);if(!on_canvas(p,radar))continue;
        bool selected=!strcmp(a.hex,n.selected);uint32_t color=a.emergency?0xff6666:a.military?0xffbb55:radar?0x77ffaa:altitude_color(a.altitude);
        unsigned opacity=a.stale_ms?(unsigned)std::max<int64_t>(40,255-(int64_t)(now-a.stale_ms)*215/30000):255;
        if(n.settings.preferences.trails) {
            unsigned end=std::min(a.trail_count,60u),begin=end>n.settings.preferences.trail_points?end-n.settings.preferences.trail_points:0;
            for(unsigned j=begin+1;j<end;j++) {
                auto p1=projection(a.trail[j-1].lat,a.trail[j-1].lon),p2=projection(a.trail[j].lat,a.trail[j].lon);
                if(on_canvas(p1,radar) && on_canvas(p2,radar))line(layer,ox+p1.x,oy+p1.y,ox+p2.x,oy+p2.y,n.settings.preferences.trail_style?altitude_color(a.trail[j].altitude):color,opacity*(j-begin)/(end-begin));
            }
        }
        if(radar && !selected && !n.settings.preferences.reduce_motion) {
            double bearing=atan2(p.x-400,209-p.y);if(bearing<0)bearing+=6.28318530718;
            double lag=fmod((now%6000)/6000.*6.28318530718-bearing+6.28318530718,6.28318530718);
            opacity=opacity*(.25+.75*(1-lag/6.28318530718));
        }
        auto direction=projection.heading(a.lat,a.lon,a.heading);
        double dx=direction.x,dy=direction.y;
        line(layer,ox+p.x-dx*5,oy+p.y-dy*5,ox+p.x+dx*8,oy+p.y+dy*8,color,opacity,2);
        line(layer,ox+p.x-dy*5,oy+p.y+dx*5,ox+p.x+dy*5,oy+p.y-dx*5,color,opacity,2);
        if(selected)ring(layer,ox+p.x,oy+p.y,13,0x55ddff,255,2);
        // Keep labels inside the viewport; a full selected summary is supplied separately.
        if(selected || (!radar && n.count<35))draw_text(layer,ox+(int)std::max(0.,std::min(690.,p.x+10)),oy+(int)std::max(0.,std::min(390.,p.y-24)),110,flight(a),selected?0x55ddff:color);
    }
}

struct Session {
    char seen[2000][7]={};unsigned unique=0,peak=0;uint64_t updated=0;
    struct Rank {char name[8];unsigned count;};Rank types[100]={},airlines[100]={};
    bool mark(const char *hex) {
        for(unsigned i=0;i<unique;i++)if(!strcmp(seen[i],hex))return false;
        if(unique==2000)return false;
        snprintf(seen[unique++],7,"%s",hex);return true;
    }
    void rank(Rank *r,const char *name) {
        if(!*name)return;
        for(unsigned i=0;i<100;i++)if(!*r[i].name || !strcmp(r[i].name,name)){snprintf(r[i].name,8,"%s",name);r[i].count++;break;}
    }
    void update(const adsb_snapshot_t &s) {
        if(!s.updated_ms || s.updated_ms==updated)return;
        updated=s.updated_ms;peak=std::max(peak,s.count);
        for(unsigned i=0;i<s.count;i++)if(mark(s.aircraft[i].hex)) {
            auto &a=s.aircraft[i];rank(types,a.type);
            if(airline(a)){char code[4]={a.callsign[0],a.callsign[1],a.callsign[2],0};rank(airlines,code);}
        }
    }
    std::string top(const Rank *r) const {
        Rank copy[100];memcpy(copy,r,sizeof(copy));std::sort(copy,copy+100,[](const Rank &a,const Rank &b){return a.count>b.count;});
        std::string out;for(unsigned i=0;i<5;i++)if(copy[i].count)out+=format("%s %u   ",copy[i].name,copy[i].count);return out.empty()?"Unavailable":out;
    }
};
}
