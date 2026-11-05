#include "ui/cities.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <string>

int main(int argc,char **argv) {
    assert(argc>=2);const char *root=argv[1];
    adsb_atlas::World world;assert(world.open(root));std::vector<uint16_t> pixels(800*418);
    if(argc==3 && std::string(argv[2])=="corrupt") {assert(!world.render({42.6975,23.3242,150},pixels.data()));return 0;}
    if(argc==3 && std::string(argv[2])=="bad-city") {adsb_cities::Database cities;assert(cities.open(root));assert(!cities.search("sofia","BG").available);return 0;}
    struct Center {double lat,lon;const char *name;};
    Center centers[]={{42.6975,23.3242,"sofia"},{40.7128,-74.006,"new-york"},{-33.8688,151.2093,"sydney"},{35.6762,139.6503,"tokyo"},{0,-140,"ocean"},{-17.7134,178.065,"fiji"},{0,179.999,"dateline"},{89,45,"arctic"},{90,0,"north-pole"},{-90,0,"south-pole"},{64.8378,-147.7164,"fairbanks"}};
    for(auto center:centers)for(unsigned range:{150u,100u,50u,20u,5u,1u}) {
        auto start=std::chrono::steady_clock::now();assert(world.render({center.lat,center.lon,range},pixels.data()));
        auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
        if(range==150)printf("%s: %lld ms\n",center.name,(long long)ms);
        if(argc==3 && range==150) {
            std::ofstream image(std::string(argv[2])+"/"+center.name+".ppm",std::ios::binary);image<<"P6\n800 418\n255\n";
            for(auto p:pixels){char c[]={char((p>>11)*255/31),char(((p>>5)&63)*255/63),char((p&31)*255/31)};image.write(c,3);}
        }
        if(std::string(center.name)=="ocean")assert(pixels[209*800+400]==adsb_atlas::ocean);
        // The pole itself has a station label at close zoom; check unlabeled land nearby.
        if(std::string(center.name)=="south-pole")assert(pixels[230*800+400]==adsb_atlas::land);
    }
    assert(!world.render({91,0,150},pixels.data()));
    assert(!world.render({NAN,0,150},pixels.data()));
    assert(!world.render({0,181,150},pixels.data()));
    assert(!world.render({0,0,0},pixels.data()));
    assert(!world.render({42,23,150},pixels.data(),[](void*){return true;}));
    // Center, longitude equivalence, and independent great-circle destination values.
    for(double latitude:{-90.,-89.,-42.,0.,42.,89.,90.})for(double longitude:{-180.,0.,180.})for(unsigned radius:{1u,150u}) {
        adsb_geo::Projection p(latitude,longitude,radius);auto origin=p(latitude,longitude);
        assert(hypot(origin.x-400,origin.y-209)<.01);
        auto wrapped=p(latitude,longitude+360);assert(hypot(wrapped.x-origin.x,wrapped.y-origin.y)<.01);
        if(fabs(latitude)==90)continue;
        for(unsigned bearing=0;bearing<360;bearing+=30) {
            double lat=latitude*adsb_geo::rad,b=bearing*adsb_geo::rad,d=radius/adsb_geo::earth_nm;
            double phi=asin(sin(lat)*cos(d)+cos(lat)*sin(d)*cos(b));
            double lon=longitude+atan2(sin(b)*sin(d)*cos(lat),cos(d)-sin(lat)*sin(phi))/adsb_geo::rad;
            auto point=p(phi/adsb_geo::rad,lon);assert(hypot(point.x-(400+400*sin(b)),point.y-(209-400*cos(b)))<.1);
        }
    }
    adsb_cities::Database cities;assert(cities.open(root));
    for(auto name:{"Sofia","London","Munich","New York","Sydney","Tokyo"}) {
        auto found=cities.search(name);assert(found.available && found.count && !strcasecmp(found.cities[0].name,name));
    }
    auto sofia=cities.search("  sOFia  ","BG",27);assert(sofia.token==27 && sofia.count==1 && !strcmp(sofia.cities[0].country,"Bulgaria"));
    auto london=cities.search("london","CA");assert(london.count && !strcmp(london.cities[0].country,"Canada"));
    auto duplicate=cities.search("springfield","US");assert(duplicate.count>1 && duplicate.count<=20);
    assert(cities.search("qzxqzxqzx").count==0);assert(cities.search("a").count==0);assert(cities.search("София").count==0);
    puts("Worldwide maps: 66 viewports, geographic projection, English city index and query bounds passed");
}
