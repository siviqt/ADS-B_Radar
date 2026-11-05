#pragma once
#include <algorithm>
#include <cmath>

namespace adsb_geo {
constexpr double rad=.017453292519943295, earth_nm=3440.065;
struct Point { double x,y; };
// Spherical azimuthal equidistant, including the date line and polar centers.
// Relative latitude arithmetic preserves sub-pixel precision on the P4 FPU.
struct Projection {
    double lat,lon;float sl,cl,scale;
    Projection(double latitude,double longitude,unsigned range,float radius=400):lat(latitude),lon(longitude),
        sl(sin(latitude*rad)),cl(cos(latitude*rad)),scale(earth_nm*radius/range) {}
    Point operator()(double latitude,double longitude) const {
        float dlat=(latitude-lat)*rad,dlon=std::remainder(longitude-lon,360.)*rad;
        float sd=sinf(dlat),cd=cosf(dlat),s=sinf(dlon),c=cosf(dlon);
        float ts=sl*cd+cl*sd,tc=cl*cd-sl*sd;
        float east=s*tc,north=sd+sl*tc*(1-c),length=hypotf(east,north);
        float factor=length>1e-12f?atan2f(length,sl*ts+cl*tc*c)/length*scale:scale;
        return {400+east*factor,209-north*factor};
    }
    Point heading(double latitude,double longitude,double degrees) const {
        // A short destination point gives local north convergence near the poles.
        double p=latitude*rad,h=degrees*rad,d=.01/earth_nm;
        double target=asin(std::clamp(sin(p)*cos(d)+cos(p)*sin(d)*cos(h),-1.,1.));
        double l=longitude+atan2(sin(h)*sin(d)*cos(p),cos(d)-sin(p)*sin(target))/rad;
        auto a=(*this)(latitude,longitude),b=(*this)(target/rad,l);
        double dx=b.x-a.x,dy=b.y-a.y,n=hypot(dx,dy);
        return n>1e-9?Point{dx/n,dy/n}:Point{sin(h),-cos(h)};
    }
};
struct Footprint {
    double south,north,longitude,half_width;
    Footprint(double lat,double lon,unsigned range):longitude(std::remainder(lon,360.)) {
        double angular=range*std::hypot(400.,209.)/400/earth_nm;
        south=std::max(-90.,lat-angular/rad-.001);north=std::min(90.,lat+angular/rad+.001);
        half_width=(south<=-90 || north>=90)?180:asin(std::min(1.,sin(angular)/cos(lat*rad)))/rad+.001;
    }
    bool intersects(double west,double south_,double east,double north_) const {
        if(north_<south || south_>north)return false;
        if(half_width>=180)return true;
        for(int shift=-1;shift<=1;shift++)
            if(east+shift*360>=longitude-half_width && west+shift*360<=longitude+half_width)return true;
        return false;
    }
};
}
