#include "ui/navigation.h"
#include <cassert>
#include <memory>
#include <cstdio>
using namespace adsb_ui;
int main() {
    auto s=std::make_unique<adsb_snapshot_t>();
    Navigation n;n.settings.lat=42.7;n.settings.lon=23.3;n.settings.radius_nm=150;
    n.settings.preferences.map_zoom=0;n.settings.preferences.radar_zoom=0;
    n.settings.preferences.cycle=true;n.settings.preferences.cycle_seconds=15;n.settings.preferences.cycle_idle_seconds=60;
    n.recenter();s->home=n.settings;
    s->count=200;
    for(unsigned i=0;i<200;i++) {
        auto &a=s->aircraft[i];snprintf(a.hex,7,"%06x",i+1);snprintf(a.callsign,9,"FLT%03u",i);
        a.lat=42.7+i*.001;a.lon=23.3;a.speed=350;a.altitude=i*100;
    }
    n.rebuild(*s);assert(n.count==200);n.move(*s,1);assert(!strcmp(n.selected,"000001"));
    std::swap(s->aircraft[0],s->aircraft[199]);n.rebuild(*s);assert(!strcmp(n.selected,"000001"));
    n.sort=Sort::Altitude;n.descending=true;n.rebuild(*s);assert(n.visible[0]==0 && n.selected_row(*s)==199);
    assert(n.open_detail(*s));adsb_enrichment_t e={};e.token=n.detail_token;snprintf(e.hex,7,"000001");assert(n.accepts(e));
    n.close();assert(!n.accepts(e));assert(n.open_detail(*s));assert(!n.accepts(e));n.close();
    n.filter=2;n.rebuild(*s);assert(n.count==0 && !*n.selected && n.selection_lost);assert(!n.open_detail(*s));
    n.filter=0;snprintf(n.query,80,"fLt010");n.rebuild(*s);assert(n.count==1);
    n.move(*s,1);assert(!strcmp(n.selected,"00000b"));
    s->aircraft[n.visible[0]].hex[0]='1';n.rebuild(*s);assert(!*n.selected && !n.open_detail(*s));
    n.query[0]=0;n.settings.preferences.map_zoom=5;n.rebuild(*s);assert(n.count<200 && n.count>0);
    n.step_range(-100);assert(n.range()==150);n.step_range(100);assert(n.range()==1);
    n.direct_view(1);assert(n.view==View::Radar && n.range()==150);n.step_range(1);assert(n.range()==100);
    n.settings.lat=43;n.rebuild(*s);assert(n.count==0);n.settings.lat=42.7;
    // Page-up on fewer than ten rows must wrap without a negative array index.
    { Navigation few;few.settings=n.settings;few.view=View::Arrivals;few.settings.preferences.arrivals_filter=4;
      auto original=s->count;s->count=3;few.rebuild(*s);few.move(*s,1);few.move(*s,-10);
      assert(few.selected_row(*s)==2);s->count=original; }
    // Radar is radial; Map is a 800:418 rectangle about the same center.
    s->count=1;s->aircraft[0].lat=42.7;s->aircraft[0].lon=23.3;n.rebuild(*s);assert(n.count==1);
    s->aircraft[0].lat=42.7+120./60;n.rebuild(*s);assert(n.count==0);
    n.direct_view(0);n.settings.preferences.map_zoom=0;n.rebuild(*s);assert(n.count==0);
    // Classification preserves the upstream filter rules, including helicopter types.
    adsb_aircraft_t a={};snprintf(a.type,8,"H145");assert(passes(a,4));a.speed=301;assert(passes(a,5));a.ground=true;assert(!passes(a,5));
    a={};a.speed=50;assert(passes(a,6));snprintf(a.callsign,9,"BAW123");assert(passes(a,1));
    a.emergency=true;assert(passes(a,3));a.military=true;assert(passes(a,2));
    // Deliberate selection, lost selection and every modal pause cycling.
    n.selected[0]=0;n.selection_lost=false;n.activity(1000);
    assert(!n.tick(60000));assert(!n.tick(61000));assert(n.tick(76000));
    auto view=n.view;snprintf(n.selected,7,"000001");assert(!n.tick(100000));assert(n.view==view);
    n.selected[0]=0;n.open(Modal::Search);assert(!n.tick(200000));assert(n.view==view);n.close();
    n.direct_view(3);n.last_action=0;n.last_cycle=300000;assert(!n.tick(304999));assert(n.tick(305000));assert(n.view==View::Map);
    // Repeats are motion only, bounded even after a long UI stall.
    KeyGate gate;assert(gate.edge(Key::Next,true,0));assert(!gate.edge(Key::Next,true,1));assert(gate.tick(10000)==Key::None);
    gate.edge(Key::Next,false,2);assert(gate.edge(Key::Next,true,3));
    assert(gate.edge(Key::Down,true,0));assert(gate.tick(399)==Key::None);assert(gate.tick(400)==Key::Down);
    assert(gate.tick(401)==Key::None);assert(gate.tick(10000)==Key::Down);assert(gate.tick(10001)==Key::None);
    gate.edge(Key::Down,false,10002);assert(gate.tick(20000)==Key::None);
    assert(gate.edge(Key::Left,true,0));gate.cancel_repeat();assert(gate.tick(400)==Key::None);
    puts("Navigation: 200-aircraft identity, disappearance, filters, search, geometry, cycling and key edges passed");
}
