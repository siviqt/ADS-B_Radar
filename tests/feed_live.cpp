#include "data/model.h"
#include <fstream>
#include <sstream>
#include <memory>
#include <cassert>
int main(int argc,char **argv) {
 assert(argc==2);std::ifstream f(argv[1]);std::stringstream body;body<<f.rdbuf();
 JsonDocument d;assert(!deserializeJson(d,body.str()));
 auto old=std::make_unique<adsb_snapshot_t>(),next=std::make_unique<adsb_snapshot_t>();
 assert(parse_aircraft(d,*next,*old,1000));assert(next->count>0);
 printf("Current adsb.fi v3 response parsed: %u aircraft\n",next->count);
}
