#!/usr/bin/env python3
"""Audit every published geometry/city record, not just sampled viewports."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import zlib

def require(condition,message):
    if not condition:raise ValueError(message)

class Reader:
    def __init__(self,data):self.data=data;self.position=0
    def take(self,n):
        require(0<=n<=len(self.data)-self.position,'truncated geometry')
        out=self.data[self.position:self.position+n];self.position+=n;return out
    def number(self):
        out=0
        for shift in range(0,35,7):
            b=self.take(1)[0];require(shift<28 or b<=15,'oversized varint');out|=(b&127)<<shift
            if not b&128:return out
        raise ValueError('unterminated varint')
    def delta(self):
        n=self.number();return (n>>1)^-(n&1)

def validate_world(root):
    index=(root/'world-v2.idx').read_bytes();geometry=(root/'world-v2-000.avp').read_bytes()
    magic,version,count,size,crc,identity=struct.unpack_from('<8sIIIIQ',index)
    require(magic==b'ADSBAT2\0' and version==2 and 0<count<=32768,'world header')
    require(len(index)==32+count*32 and zlib.crc32(index[32:])==crc,'world index CRC/size')
    require(geometry[:8]==b'ADSBG2\0\0' and len(geometry)==16+size and geometry[8:16]==index[24:32]==hashlib.sha256(geometry[16:]).digest()[:8],'world identity/size')
    bounds=[[],[],[]];offset_expected=0;records=paths=vertices=0
    for offset in range(32,len(index),32):
        w,s,e,n,start,length,checksum,lod=struct.unpack_from('<iiiiIIII',index,offset)
        require(0<=lod<3 and -18000000<=w<e<=18000000 and -9000000<=s<n<=9000000,'cell bounds')
        require(start==offset_expected and 4<=length<=65536 and start+length<=size,'geometry extent')
        offset_expected+=length;bounds[lod].append((w,s,e,n));block=geometry[16+start:16+start+length]
        require(zlib.crc32(block)==checksum,'block CRC');reader=Reader(block)
        count_records=struct.unpack('<I',reader.take(4))[0];require(count_records<=12000,'record bound');block_vertices=0
        for _ in range(count_records):
            records+=1;kind=reader.take(1)[0];require(kind<=5,'feature kind')
            if kind==5:
                x,y,population,chars=struct.unpack('<iiIB',reader.take(13));name=reader.take(chars)
                require(0<chars<=47 and all(32<=b<127 for b in name) and w<=x<=e and s<=y<=n,'label record');continue
            rings=reader.number();require(0<rings<=512,'ring count');projected=0
            for _ in range(rings):
                paths+=1;points=reader.number();require((4 if kind<2 else 2)<=points<=24000,'degenerate or oversized path')
                vertices+=points;block_vertices+=points;x=y=0;previous=None
                for __ in range(points):
                    x+=reader.delta();y+=reader.delta();require(w<=x<=e and s<=y<=n,'point outside cell')
                    projected+=max(1,math.ceil(max(abs(x-previous[0]),abs(y-previous[1]))/10000)) if previous else 1
                    previous=x,y
                require(projected<=60000,'projected vertex bound')
            require(block_vertices<=24000,'block vertex bound')
        require(reader.position==len(block),'trailing geometry')
    require(offset_expected==size,'unreferenced geometry')
    for cells in bounds:
        edges=sorted({value for _,s,_,n in cells for value in (s,n)})
        require(edges[0]==-9000000 and edges[-1]==9000000,'latitude coverage')
        for south,north in zip(edges,edges[1:]):
            middle=south+north;intervals=sorted((w,e) for w,s,e,n in cells if s*2<middle<n*2);cursor=-18000000
            for west,east in intervals:require(west==cursor,'coverage gap or overlapping cells');cursor=east
            require(cursor==18000000,'longitude coverage')
    return dict(blocks=count,records=records,paths=paths,vertices=vertices,full_domain_all_lods=True)

def validate_cities(root):
    index=(root/'cities-v1.idx').read_bytes();data=(root/'cities-v1.dat').read_bytes()
    magic,version,names,records,crc,identity=struct.unpack_from('<8sIIIIQ',index)
    require(magic==b'ADSBCI1\0' and version==1 and 0<names<=2000000 and 0<records<=1000000,'city header')
    require(len(index)==32+names*80 and zlib.crc32(index[32:])==crc,'city index size/CRC')
    require(data[:8]==b'ADSBCD1\0' and len(data)==16+records*120 and data[8:16]==index[24:32]==hashlib.sha256(data[16:]).digest()[:8],'city data identity/size')
    last_id=0
    for offset in range(16,len(data),120):
        record=data[offset:offset+120];gid,lat,lon,population=struct.unpack_from('<IiiI',record)
        require(zlib.crc32(record[:116])==struct.unpack_from('<I',record,116)[0],'city CRC')
        require(gid>last_id and abs(lat)<=90000000 and abs(lon)<=180000000,'city ID/coordinate');last_id=gid
        for field in (record[16:64],record[64:104],record[104:116]):
            require(b'\0' in field and all(32<=c<127 for c in field.split(b'\0')[0]),'non-ASCII/unterminated city string')
    last=b''
    for offset in range(32,len(index),80):
        record=index[offset:offset+80];row=struct.unpack_from('<I',record,64)[0]
        require(zlib.crc32(record[:76])==struct.unpack_from('<I',record,76)[0] and row<records,'name CRC/row')
        require(b'\0' in record[:64],'unterminated name');name=record[:64].split(b'\0')[0]
        require(name and name>=last and name.lower()==name and all(32<=c<127 for c in name),'name order/ASCII');last=name
    return dict(cities=records,english_names=names)

def validate(root):return dict(world=validate_world(root),cities=validate_cities(root))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('root',type=Path);args=parser.parse_args()
    print(json.dumps(validate(args.root),indent=2))
