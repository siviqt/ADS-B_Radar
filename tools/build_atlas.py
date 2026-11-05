#!/usr/bin/env python3
"""Build deterministic, bounded worldwide SD assets from pinned local source data.

No network access. Geometry blocks use signed delta varints (no runtime codec).
Natural Earth: public domain; GeoNames: CC BY 4.0. See generated DATA_NOTICES.txt.
"""
import argparse
from collections import defaultdict
import hashlib
import io
import json
import math
from pathlib import Path
import struct
import unicodedata
import zipfile
import zlib

from shapely import make_valid
from shapely.geometry import shape, box
from shapely.strtree import STRtree
import validate_atlas

SCALE=100000
TOLERANCES=(.0005,.002,.008)
MAX_BLOCK=65536
MAX_VERTICES=24000

def ascii_name(s):
    s=(s or '').replace('ß','ss').replace('ø','o').replace('Ø','O').replace('ł','l').replace('Ł','L')
    return ' '.join(unicodedata.normalize('NFKD',s).encode('ascii','ignore').decode().split())

def key(s):
    return ascii_name(s).lower()

def digest(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for data in iter(lambda:f.read(1048576),b''):h.update(data)
    return h.hexdigest()

def varuint(n):
    out=bytearray()
    while n>=128:out.append((n&127)|128);n>>=7
    out.append(n);return out

def signed(n):return varuint((n<<1)^(n>>31))

def parts(g,polygon):
    if g.is_empty:return
    if g.geom_type==('Polygon' if polygon else 'LineString'):yield g
    elif hasattr(g,'geoms'):
        for sub in g.geoms:yield from parts(sub,polygon)

def geometry_record(kind,g):
    rings=[g.exterior,*g.interiors] if kind<2 else [g]
    paths=[]
    for index,ring in enumerate(rings):
        points=[]
        for p in ring.coords:
            q=(round(p[0]*SCALE),round(p[1]*SCALE))
            if not points or q!=points[-1]:points.append(q)
        # Clipping at a cell corner can produce a nonzero floating-point line
        # shorter than our integer grid. Do not publish a degenerate primitive.
        if len(points)<(4 if kind<2 else 2):
            if index==0:return None,0
            continue
        paths.append(points)
    out=bytearray([kind]);out+=varuint(len(paths));vertices=0
    for points in paths:
        out+=varuint(len(points));vertices+=len(points);x=y=0
        for a,b in points:out+=signed(a-x);out+=signed(b-y);x,y=a,b
    return out,vertices

def build_world(source,output):
    entries=[];data=bytearray();max_block=0;max_vertices=0
    originals=[]
    for kind,name in enumerate(('land','lakes','rivers','roads','borders')):
        features=json.loads((source/(name+'.geojson')).read_text())['features']
        geometries=[]
        for f in features:
            p=f['properties']
            if name=='roads' and not (p.get('type')=='Major Highway' or p.get('scalerank',99)<=5):continue
            g=make_valid(shape(f['geometry']))
            geometries.extend(parts(g,kind<2))
        originals.append(geometries)
    places=json.loads((source/'places.geojson').read_text())['features']
    labels=[]
    for f in places:
        p=f['properties'];name=ascii_name(p.get('NAME_EN') or p.get('NAMEASCII') or p.get('NAME'))[:47]
        if name:labels.append((f['geometry']['coordinates'],name,max(0,int(p.get('POP_MAX',0)))))
    for lod,tolerance in enumerate(TOLERANCES):
        layers=[]
        for kind,geometries in enumerate(originals):
            geoms=[g.simplify(tolerance,preserve_topology=True) for g in geometries]
            layers.append((geoms,STRtree(geoms)))
        def cell(w,s,e,n,depth=0):
            nonlocal max_block,max_vertices
            rectangle=box(w,s,e,n);records=[];vertices=0
            for kind,(geoms,tree) in enumerate(layers):
                for index in sorted(tree.query(rectangle,predicate='intersects')):
                    clipped=geoms[index].intersection(rectangle)
                    for g in parts(clipped,kind<2):
                        record,count=geometry_record(kind,g)
                        if record is not None:records.append(record);vertices+=count
            for (x,y),name,pop in labels:
                if w<=x<e and s<=y<n:
                    encoded=name.encode();records.append(bytes([5])+struct.pack('<iiIB',round(x*SCALE),round(y*SCALE),pop,len(encoded))+encoded)
            block=struct.pack('<I',len(records))+b''.join(records)
            if len(block)>MAX_BLOCK or vertices>MAX_VERTICES:
                if depth>=6:raise ValueError('cell exceeds decoder bound')
                mx=(w+e)/2;my=(s+n)/2
                for bounds in ((w,s,mx,my),(mx,s,e,my),(w,my,mx,n),(mx,my,e,n)):cell(*bounds,depth+1)
                return
            entries.append(struct.pack('<iiiiIIII',round(w*SCALE),round(s*SCALE),round(e*SCALE),round(n*SCALE),len(data),len(block),zlib.crc32(block),lod))
            data.extend(block);max_block=max(max_block,len(block));max_vertices=max(max_vertices,vertices)
        for south in range(-90,90,5):
            for west in range(-180,180,5):cell(west,south,west+5,south+5)
        print(f'world LOD {lod}: {len(entries)} blocks, {len(data):,} bytes',flush=True)
    if len(entries)>32768:raise ValueError('index exceeds runtime bound')
    identity=int.from_bytes(hashlib.sha256(data).digest()[:8],'little')
    raw=b''.join(entries)
    (output/'world-v2.idx').write_bytes(struct.pack('<8sIIIIQ',b'ADSBAT2\0',2,len(entries),len(data),zlib.crc32(raw),identity)+raw)
    (output/'world-v2-000.avp').write_bytes(struct.pack('<8sQ',b'ADSBG2\0\0',identity)+data)
    return dict(blocks=len(entries),max_block_bytes=max_block,max_block_vertices=max_vertices,lods=list(TOLERANCES))

def build_cities(source,output):
    cities={}
    with zipfile.ZipFile(source/'cities500.zip') as z:
        for line in io.TextIOWrapper(z.open('cities500.txt'),encoding='utf8'):
            p=line.rstrip('\n').split('\t');cities[int(p[0])]=p
    # Read the source stream once; only English variants for retained city IDs survive.
    english=defaultdict(list)
    with zipfile.ZipFile(source/'alternateNamesV2.zip') as z:
        for line in io.TextIOWrapper(z.open('alternateNamesV2.txt'),encoding='utf8'):
            p=line.rstrip('\n').split('\t')
            if p[2]=='en' and int(p[1]) in cities:
                if len(p)>7 and (p[6]=='1' or p[7]=='1'):continue  # colloquial/historic
                name=ascii_name(p[3])
                if name:english[int(p[1])].append((p[4]=='1',name))
    countries={}
    for line in (source/'countryInfo.txt').read_text().splitlines():
        if line and not line.startswith('#'):
            p=line.split('\t');countries[p[0]]=ascii_name(p[4])
    # Admin codes disambiguate regions without introducing non-English labels.
    records=[];names=[]
    for gid,p in sorted(cities.items()):
        options=sorted(set(english[gid]),key=lambda v:(not v[0],v[1]))
        name=options[0][1] if options else ascii_name(p[2] or p[1])
        if not name:continue
        country=countries.get(p[8],p[8]);region=p[10]
        record=struct.pack('<IiiI48s40s12s',gid,round(float(p[4])*1e6),round(float(p[5])*1e6),min(int(p[14]),0xffffffff),name.encode()[:47],country.encode()[:39],region.encode()[:11])
        row=len(records);records.append(record+struct.pack('<I',zlib.crc32(record)))
        variants={key(name)}|{key(v[1]) for v in options}
        for variant in sorted(variants):
            if variant:names.append((variant[:63],row,min(int(p[14]),0xffffffff),p[8]))
    names=sorted(set(names));raw=bytearray()
    for n,row,pop,c in names:
        entry=struct.pack('<64sII4s',n.encode(),row,pop,c.encode());raw+=entry+struct.pack('<I',zlib.crc32(entry))
    data=b''.join(records);identity=int.from_bytes(hashlib.sha256(data).digest()[:8],'little')
    (output/'cities-v1.idx').write_bytes(struct.pack('<8sIIIIQ',b'ADSBCI1\0',1,len(names),len(records),zlib.crc32(raw),identity)+raw)
    (output/'cities-v1.dat').write_bytes(struct.pack('<8sQ',b'ADSBCD1\0',identity)+data)
    return dict(cities=len(records),english_names=len(names),record_bytes=120,index_record_bytes=80)

def build(source,output,world=True,cities=True):
    output.mkdir(parents=True,exist_ok=True)
    report=json.loads((output/'atlas-v2.json').read_text()) if (output/'atlas-v2.json').exists() else {}
    if world:report['world']=build_world(source,output)
    if cities:report['cities']=build_cities(source,output)
    (output/'DATA_NOTICES.txt').write_text('Worldwide ADS-B basemap: Natural Earth, public domain.\nhttps://www.naturalearthdata.com/about/terms-of-use/\nCity search: GeoNames, Creative Commons Attribution 4.0.\nhttps://www.geonames.org/ https://creativecommons.org/licenses/by/4.0/\nModified: selected cities500, English names/ASCII spellings, normalized indexed binary records.\nNo accuracy, currency or completeness guarantee is provided by GeoNames.\n'+'\nMap-label glyphs: DejaVu Sans Mono, rasterized ASCII subset.\n'+(Path(__file__).parent/'licenses/DEJAVU.txt').read_text())
    report['validation']={}
    if (output/'world-v2.idx').exists():report['validation']['world']=validate_atlas.validate_world(output)
    if (output/'cities-v1.idx').exists():report['validation']['cities']=validate_atlas.validate_cities(output)
    report.update(version=2,coverage=[-180,-90,180,90],language='en',projection='spherical-azimuthal-equidistant',sources={p.name:digest(p) for p in sorted(source.iterdir()) if p.is_file()},files={p.name:dict(bytes=p.stat().st_size,sha256=digest(p),crc32=zlib.crc32(p.read_bytes())) for p in sorted(output.iterdir()) if p.is_file() and p.name!='atlas-v2.json'})
    (output/'atlas-v2.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
    print(json.dumps({k:report[k] for k in ('world','cities','files') if k in report},indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--only',choices=('world','cities'));a=p.parse_args();build(a.source,a.output,a.only!='cities',a.only!='world')
