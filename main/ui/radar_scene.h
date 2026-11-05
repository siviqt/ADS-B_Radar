#pragma once
#include "screens.h"

namespace adsb_ui {
// Rasterize rings, trails and aircraft when data/projection/selection changes.
// Animation draws only a bounded number of sweep strokes, independent of traffic.
struct RadarScene {
    struct Plane { unsigned index;Position p;float dx,dy; } planes[200];
    unsigned count=0,builds=0;
    uint16_t *base_pixels=nullptr;uint64_t last_sweep=0;bool sweep_drawn=false;
    std::string key;
    bool update(lv_obj_t *canvas,const Navigation &n,const adsb_snapshot_t &s,uint64_t now) {
        bool stale=false;for(unsigned i=0;i<n.count;i++)stale|=s.aircraft[n.visible[i]].stale_ms!=0;
        auto next=format("%llu/%u/%.10f/%.10f/%u/%u/%s/%u/%u/%u/%llu",(unsigned long long)s.updated_ms,s.count,n.center_lat,n.center_lon,n.range(),n.filter,n.query,n.settings.preferences.trails,n.settings.preferences.trail_points,n.settings.preferences.trail_style,(unsigned long long)(stale?now/1000:0));
        for(unsigned i=0;i<n.count;i++)next+=format("/%u",n.visible[i]);
        next+="/"+std::string(n.selected);
        if(key==next)return false;
        key=next;count=0;builds++;RadarProjection projection(n);
        lv_canvas_fill_bg(canvas,lv_color_hex(0x0b1821),LV_OPA_COVER);
        lv_layer_t layer;lv_canvas_init_layer(canvas,&layer);
        for(unsigned r=1;r<=4;r++)ring(&layer,200,209,199*r/4,0x20563f);
        line(&layer,1,209,399,209,0x20563f);line(&layer,200,10,200,408,0x20563f);
        draw_text(&layer,194,10,25,"N",0x70c99c);
        for(unsigned r=1;r<4;r++)draw_text(&layer,204,209-199*r/4,90,format("%g",n.range()*r/4.).c_str(),0x70c99c);
        auto home=projection(n.settings.lat,n.settings.lon);
        if(on_canvas(home,true)) {line(&layer,home.x-206,home.y,home.x-194,home.y,0xffffff);line(&layer,home.x-200,home.y-6,home.x-200,home.y+6,0xffffff);}
        for(unsigned i=0;i<n.count;i++) {
            const auto &a=s.aircraft[n.visible[i]];auto p=projection(a.lat,a.lon);if(!on_canvas(p,true))continue;
            auto direction=projection.heading(a.lat,a.lon,a.heading);
            planes[count++]={n.visible[i],p,(float)direction.x,(float)direction.y};
            if(!n.settings.preferences.trails)continue;
            uint32_t color=a.emergency?0xff6666:a.military?0xffbb55:0x77ffaa;
            unsigned opacity=a.stale_ms?(unsigned)std::max<int64_t>(40,255-(int64_t)(now-a.stale_ms)*215/30000):255;
            unsigned end=std::min(a.trail_count,60u),begin=end>n.settings.preferences.trail_points?end-n.settings.preferences.trail_points:0;
            if(end<=begin+1)continue;
            auto previous=projection(a.trail[begin].lat,a.trail[begin].lon);
            for(unsigned j=begin+1;j<end;j++) {
                auto current=projection(a.trail[j].lat,a.trail[j].lon);
                // Repeated/subpixel positions do not need separate draw tasks.
                if(on_canvas(previous,true) && on_canvas(current,true) && (lround(previous.x)!=lround(current.x) || lround(previous.y)!=lround(current.y)))
                    line(&layer,previous.x-200,previous.y,current.x-200,current.y,n.settings.preferences.trail_style?altitude_color(a.trail[j].altitude):color,opacity*(j-begin)/(end-begin));
                previous=current;
            }
        }
        for(unsigned i=0;i<count;i++) {
            const auto &plane=planes[i];const auto &a=s.aircraft[plane.index];
            bool selected=!strcmp(a.hex,n.selected);
            uint32_t color=a.emergency?0xff6666:a.military?0xffbb55:0x77ffaa;
            unsigned opacity=a.stale_ms?(unsigned)std::max<int64_t>(40,255-(int64_t)(now-a.stale_ms)*215/30000):255;
            double x=plane.p.x-200,y=plane.p.y,dx=plane.dx,dy=plane.dy;
            line(&layer,x-dx*5,y-dy*5,x+dx*8,y+dy*8,color,opacity,2);
            line(&layer,x-dy*5,y+dx*5,x+dy*5,y-dx*5,color,opacity,2);
            if(selected) {
                ring(&layer,x,y,13,0x55ddff,255,2);
                draw_text(&layer,(int)std::max(0.,std::min(290.,plane.p.x-190)),(int)std::max(0.,std::min(390.,plane.p.y-24)),110,flight(a),0x55ddff);
            }
        }
        lv_canvas_finish_layer(canvas,&layer);
        return true;
    }
    // Invalidate just the swept wedge's previous/new bounding rectangle, not the entire radar.
    static lv_area_t sweep_area(uint64_t now,int ox,int oy) {
        float angle=(now%6000)/6000.f*6.28318530718f;
        lv_area_t a={ox+200,oy+209,ox+200,oy+209};
        for(int i=0;i<=6;i++) {
            float t=angle-i*.05f;int x=(int)lroundf(ox+200+199*sinf(t)),y=(int)lroundf(oy+209-199*cosf(t));
            a.x1=std::min<int32_t>(a.x1,x);a.x2=std::max<int32_t>(a.x2,x);a.y1=std::min<int32_t>(a.y1,y);a.y2=std::max<int32_t>(a.y2,y);
        }
        a.x1-=2;a.x2+=2;a.y1-=2;a.y2+=2;return a;
    }
    static uint16_t blend(uint16_t background,unsigned opacity) {
        // Sweep green 0x44cc88, blended directly into RGB565 without transient draw tasks.
        unsigned r=((background>>11)*(255-opacity)+8*opacity+127)/255;
        unsigned g=(((background>>5)&63)*(255-opacity)+51*opacity+127)/255;
        unsigned b=((background&31)*(255-opacity)+17*opacity+127)/255;
        return (r<<11)|(g<<5)|b;
    }
    static void stroke(uint16_t *pixels,int x,int y,unsigned opacity) {
        int x0=200,y0=209,dx=abs(x-x0),sx=x0<x?1:-1,dy=-abs(y-y0),sy=y0<y?1:-1,error=dx+dy;
        for(;;) {
            if(x0>=0 && x0<400 && y0>=0 && y0<418) {
                auto &p=pixels[y0*400+x0];p=blend(p,opacity);
            }
            if(x0==x && y0==y)break;
            int twice=error*2;if(twice>=dy){error+=dy;x0+=sx;}if(twice<=dx){error+=dx;y0+=sy;}
        }
    }
    static lv_area_t local_area(uint64_t now) {
        auto a=sweep_area(now,0,0);
        a.x1=std::max<int32_t>(0,a.x1);a.x2=std::min<int32_t>(399,a.x2);
        a.y1=std::max<int32_t>(0,a.y1);a.y2=std::min<int32_t>(417,a.y2);return a;
    }
    void animate(lv_obj_t *canvas,uint64_t now,bool motion) {
        if(!base_pixels)return;
        auto pixels=(uint16_t*)lv_canvas_get_draw_buf(canvas)->data;
        auto previous=local_area(last_sweep),current=local_area(now);
        if(sweep_drawn)for(int y=previous.y1;y<=previous.y2;y++)
            memcpy(pixels+y*400+previous.x1,base_pixels+y*400+previous.x1,(previous.x2-previous.x1+1)*2);
        if(motion) {
            float angle=(now%6000)/6000.f*6.28318530718f;
            for(int i=6;i>=0;i--) {
                float a=angle-i*.05f;
                stroke(pixels,lroundf(200+199*sinf(a)),lroundf(209-199*cosf(a)),(7-i)*12);
            }
        }
        lv_area_t coords;lv_obj_get_coords(canvas,&coords);
        auto invalidate=[&](lv_area_t a){a.x1+=coords.x1;a.x2+=coords.x1;a.y1+=coords.y1;a.y2+=coords.y1;lv_obj_invalidate_area(canvas,&a);};
        if(sweep_drawn)invalidate(previous);
        if(motion)invalidate(current);
        sweep_drawn=motion;last_sweep=now;
    }
    void sync(lv_obj_t *canvas,lv_obj_t *base,uint64_t now,bool motion) {
        base_pixels=(uint16_t*)lv_canvas_get_draw_buf(base)->data;
        memcpy(lv_canvas_get_draw_buf(canvas)->data,base_pixels,400*418*2);
        sweep_drawn=false;animate(canvas,now,motion);lv_obj_invalidate(canvas);
    }
};
}
