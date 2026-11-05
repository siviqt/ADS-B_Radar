#pragma once
#include "map_pack.h"
#include "geography.h"
#include "map_font.h"
#include <algorithm>
#include <array>
#include <vector>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

namespace adsb_atlas {
// All sizeable containers explicitly use PSRAM on the device.
template<class T> struct Alloc {
    using value_type=T;
    Alloc()=default;template<class U>Alloc(const Alloc<U>&) {}
    T *allocate(size_t n) {
        if(n>SIZE_MAX/sizeof(T))throw std::bad_alloc();
#ifdef ESP_PLATFORM
        auto p=heap_caps_malloc(n*sizeof(T),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
        auto p=malloc(n*sizeof(T));
#endif
        if(!p)throw std::bad_alloc();
        return static_cast<T*>(p);
    }
    void deallocate(T *p,size_t){free(p);}
    template<class U>bool operator==(const Alloc<U>&)const{return true;}
    template<class U>bool operator!=(const Alloc<U>&)const{return false;}
};
template<class T>using Vec=std::vector<T,Alloc<T>>;
inline uint32_t u32(const uint8_t *p){return p[0]|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
inline int32_t i32(const uint8_t *p){return static_cast<int32_t>(u32(p));}
inline bool exact(FILE *f,void *p,size_t n){return fread(p,1,n,f)==n && !ferror(f);}
inline long file_size(FILE *f){if(fseek(f,0,SEEK_END))return -1;long n=ftell(f);rewind(f);return n;}
struct File {
    FILE *p=nullptr;~File(){if(p)fclose(p);}File()=default;File(const File&)=delete;File&operator=(const File&)=delete;
    bool open(const std::string &path){if(p)fclose(p);p=fopen(path.c_str(),"rb");return p;}
    void close(){if(p)fclose(p);p=nullptr;}
};
constexpr uint16_t rgb(unsigned c){return ((c>>19)&31)<<11|((c>>10)&63)<<5|((c>>3)&31);}
constexpr uint16_t ocean=rgb(0x101f2a),land=rgb(0x192e30),water=rgb(0x214b62),border=rgb(0x607a6c),road=rgb(0x72654b),river=rgb(0x2b5265),ink=rgb(0xc2d1c5);
struct Raster {
    uint16_t *pixels;
    void pixel(int x,int y,uint16_t c){if(x>=0 && x<800 && y>=0 && y<418)pixels[y*800+x]=c;}
    void line(adsb_geo::Point a,adsb_geo::Point b,uint16_t color) {
        if(!std::isfinite(a.x+a.y+b.x+b.y))return;
        double dx=b.x-a.x,dy=b.y-a.y,lo=0,hi=1;
        auto clip=[&](double p,double q){if(p==0)return q>=0;double t=q/p;if(p<0)lo=std::max(lo,t);else hi=std::min(hi,t);return lo<=hi;};
        if(!clip(-dx,a.x)||!clip(dx,799-a.x)||!clip(-dy,a.y)||!clip(dy,417-a.y))return;
        int x=lround(a.x+lo*dx),y=lround(a.y+lo*dy),ex=lround(a.x+hi*dx),ey=lround(a.y+hi*dy);
        int ax=abs(ex-x),ay=-abs(ey-y),sx=x<ex?1:-1,sy=y<ey?1:-1,error=ax+ay;
        for(;;){pixel(x,y,color);if(x==ex && y==ey)break;int twice=error*2;if(twice>=ay){error+=ay;x+=sx;}if(twice<=ax){error+=ax;y+=sy;}}
    }
    void polygon(const Vec<adsb_geo::Point> &points,const Vec<unsigned> &ends,uint16_t color) {
        double ymin=418,ymax=0;for(auto p:points){ymin=std::min(ymin,p.y);ymax=std::max(ymax,p.y);}
        int first=std::max(0,(int)floor(std::clamp(ymin,-1e8,1e8))),last=std::min(417,(int)ceil(std::clamp(ymax,-1e8,1e8)));
        Vec<float> intersections;intersections.reserve(std::min(points.size(),size_t(24000)));
        for(int y=first;y<=last;y++) {
            intersections.clear();unsigned begin=0;
            for(unsigned end:ends) {
                for(unsigned i=begin,j=end-1;i<end;j=i++) {
                    auto a=points[i],b=points[j];double scan=y+.5;
                    if((a.y<=scan && b.y>scan)||(b.y<=scan && a.y>scan))intersections.push_back(a.x+(scan-a.y)*(b.x-a.x)/(b.y-a.y));
                }
                begin=end;
            }
            std::sort(intersections.begin(),intersections.end());
            for(size_t i=1;i<intersections.size();i+=2) {
                int x=std::max(0,(int)ceil(std::clamp(double(intersections[i-1])-.5,-1e8,1e8))),end=std::min(799,(int)floor(std::clamp(double(intersections[i])-.5,-1e8,1e8)));
                if(x<=end)std::fill(pixels+y*800+x,pixels+y*800+end+1,color);
            }
        }
    }
    void text(int x,int y,const char *s){for(;*s;s++,x+=8)if(*s>=32 && *s<127)for(int row=0;row<14;row++)for(int col=0;col<8;col++)if(font[*s-32][row]&(1<<col)){pixel(x+col+1,y+row+1,ocean);pixel(x+col,y+row,ink);}}
};
struct Decoder {
    const uint8_t *p,*end;bool good=true;
    uint32_t number(){uint32_t n=0;for(unsigned i=0;i<5;i++){if(p==end){good=false;return 0;}uint8_t b=*p++;if(i==4 && b>15){good=false;return 0;}n|=uint32_t(b&127)<<(i*7);if(!(b&128))return n;}good=false;return 0;}
    int32_t delta(){auto n=number();return (n>>1)^-int32_t(n&1);}
};
struct Label {adsb_geo::Point p;uint32_t population;char name[48];};
class World {
    File geometry;Vec<uint8_t> index;
    struct Cache {uint32_t offset=UINT32_MAX;Vec<uint8_t> bytes;};
    std::array<Cache,8> cache;unsigned slot=0;uint32_t count=0;
    const uint8_t *block(const uint8_t *entry) {
        uint32_t offset=u32(entry+16),size=u32(entry+20);
        for(auto &c:cache)if(c.offset==offset)return c.bytes.data();
        auto &c=cache[slot++%cache.size()];c.offset=UINT32_MAX;c.bytes.resize(size);
        if(fseek(geometry.p,16+offset,SEEK_SET)||!exact(geometry.p,c.bytes.data(),size)||adsb_maps::crc32(c.bytes.data(),size)!=u32(entry+24))return nullptr;
        c.offset=offset;return c.bytes.data();
    }
public:
    void close(){geometry.close();index.clear();count=0;for(auto &c:cache){c.offset=UINT32_MAX;c.bytes.clear();}}
    bool ready()const{return geometry.p && count;}
    bool open(const char *root) {
        close();File f;if(!f.open(std::string(root)+"/world-v2.idx"))return false;
        uint8_t h[32],g[16];long size=file_size(f.p);
        if(!exact(f.p,h,32)||memcmp(h,"ADSBAT2\0",8)||u32(h+8)!=2)return false;
        uint32_t entries=u32(h+12),bytes=u32(h+16);
        if(!entries || entries>32768 || size!=32+long(entries)*32 || bytes>268435456)return false;
        index.resize(entries*32);
        if(!exact(f.p,index.data(),index.size())||adsb_maps::crc32(index.data(),index.size())!=u32(h+20))return false;
        if(!geometry.open(std::string(root)+"/world-v2-000.avp"))return false;
        if(file_size(geometry.p)!=16+long(bytes)||!exact(geometry.p,g,16)||memcmp(g,"ADSBG2\0\0",8)||memcmp(g+8,h+24,8)){close();return false;}
        for(uint32_t i=0;i<entries;i++) {
            auto e=index.data()+i*32;uint32_t o=u32(e+16),n=u32(e+20);
            if(i32(e)<-18000000||i32(e+8)>18000000||i32(e+4)<-9000000||i32(e+12)>9000000||i32(e)>=i32(e+8)||i32(e+4)>=i32(e+12)||n<4||n>65536||o>bytes||n>bytes-o||u32(e+28)>2){close();return false;}
        }
        count=entries;return true;
    }
    bool render(adsb_maps::Request request,uint16_t *pixels,bool (*cancel)(void*)=nullptr,void *context=nullptr) {
        if(!ready() || !pixels || !std::isfinite(request.lat+request.lon)||fabs(request.lat)>90||fabs(request.lon)>180||!request.range)return false;
        Raster raster{pixels};std::fill(pixels,pixels+800*418,ocean);
        adsb_geo::Projection project(request.lat,request.lon,request.range);adsb_geo::Footprint bounds(request.lat,request.lon,request.range);
        unsigned lod=request.range>=100?2:request.range>=20?1:0;Vec<Label> labels;
        Vec<adsb_geo::Point> points;Vec<unsigned> ends;points.reserve(24000);
        for(uint32_t i=0;i<count;i++) {
            auto e=index.data()+i*32;
            if(u32(e+28)!=lod || !bounds.intersects(i32(e)/1e5,i32(e+4)/1e5,i32(e+8)/1e5,i32(e+12)/1e5))continue;
            if(cancel && cancel(context))return false;
            auto p=block(e);if(!p)return false;
            Decoder d{p+4,p+u32(e+20)};uint32_t records=u32(p),vertices=0;if(records>12000)return false;
            for(uint32_t record=0;record<records;record++) {
                if(d.p==d.end)return false;
                uint8_t kind=*d.p++;
                if(kind==5) {
                    if(d.end-d.p<13)return false;
                    int32_t x=i32(d.p),y=i32(d.p+4);uint32_t pop=u32(d.p+8);unsigned n=d.p[12];d.p+=13;
                    if(!n||n>47||d.end-d.p<n||x < -18000000 || x>18000000 || y < -9000000 || y>9000000)return false;
                    Label label{project(y/1e5,x/1e5),pop,{}};memcpy(label.name,d.p,n);d.p+=n;
                    if(label.p.x>=0 && label.p.x<800 && label.p.y>=0 && label.p.y<418 && (request.range<50 || pop>=40000)) {
                        if(labels.size()<256)labels.push_back(label);
                        else {auto least=std::min_element(labels.begin(),labels.end(),[](const Label&a,const Label&b){return a.population<b.population;});if(pop>least->population)*least=label;}
                    }
                    continue;
                }
                if(kind>4)return false;
                unsigned rings=d.number();if(!d.good||!rings||rings>512)return false;points.clear();ends.clear();
                for(unsigned ring=0;ring<rings;ring++) {
                    unsigned n=d.number();if(!d.good || n<2 || n>24000 || vertices+n>24000)return false;vertices+=n;int64_t x=0,y=0;
                    double previous_x=0,previous_y=0;
                    for(unsigned j=0;j<n;j++) {
                        x+=d.delta();y+=d.delta();if(!d.good||std::abs(x)>18000000||std::abs(y)>9000000)return false;
                        double longitude=x/1e5,latitude=y/1e5;
                        // Densify clipping boundaries before projection; long geographic edges curve.
                        unsigned steps=j?std::max(1u,(unsigned)ceil(std::max(fabs(longitude-previous_x),fabs(latitude-previous_y))/.1)):1;
                        if(steps>3600 || points.size()+steps>60000)return false;
                        for(unsigned step=1;step<=steps;step++)points.push_back(project(j?previous_y+(latitude-previous_y)*step/steps:latitude,j?previous_x+(longitude-previous_x)*step/steps:longitude));
                        previous_x=longitude;previous_y=latitude;
                    }
                    ends.push_back(points.size());
                }
                if(kind<2)raster.polygon(points,ends,kind==0?land:water);
                else {unsigned begin=0;for(unsigned end:ends){for(unsigned j=begin+1;j<end;j++)raster.line(points[j-1],points[j],kind==2?river:kind==3?road:border);begin=end;}}
            }
            if(d.p!=d.end || !d.good)return false;
        }
        std::sort(labels.begin(),labels.end(),[](const Label&a,const Label&b){return a.population>b.population;});
        struct Box{int x,y,w;};std::array<Box,32> placed;unsigned placed_count=0;
        for(const auto &l:labels) {
            if(placed_count==placed.size())break;
            int w=strlen(l.name)*8,x=std::clamp((int)l.p.x+6,0,std::max(0,799-w)),y=std::clamp((int)l.p.y-14,0,403);bool overlap=false;
            for(unsigned j=0;j<placed_count;j++){auto b=placed[j];if(x<b.x+b.w+5 && x+w+5>b.x && y<b.y+18 && y+18>b.y)overlap=true;}
            if(overlap)continue;
            placed[placed_count++]={x,y,w};raster.pixel(l.p.x,l.p.y,ink);raster.text(x,y,l.name);
        }
        return true;
    }
};
}
