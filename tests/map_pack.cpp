#include "ui/map_pack.h"
#include <cassert>
int main(int argc,char **argv) {
    assert(argc==3);
    assert(adsb_maps::same({42.6975,23.3242,1},{42.6974983215332,23.3241996765,1}));
    assert(!adsb_maps::same({42.6975,23.3242,1},{42.6976,23.3242,1}));uint8_t *pixels=new uint8_t[adsb_maps::bytes];char credit[160];
    bool valid=adsb_maps::load(argv[1],{0,0,1},pixels,credit,sizeof(credit));
    assert(valid==(std::string(argv[2])=="valid"));
    assert(!adsb_maps::load(argv[1],{0.01,0,1},pixels,credit,sizeof(credit)));
    assert(!adsb_maps::load(argv[1],{0,0,5},pixels,credit,sizeof(credit)));
    if(valid){assert(pixels[0]==0 && pixels[1]==0xf8);assert(std::string(credit)=="Synthetic test tiles");}
    delete[] pixels;
}
