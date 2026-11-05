#!/usr/bin/env python3
"""Generate native map packs using the exact aircraft projection, from cached XYZ tiles.

No provider is selected and no tiles are downloaded. Obtain tiles under the provider's
terms, including permission to redistribute, then supply its required attribution.
"""
import argparse
from functools import lru_cache
import json
import math
from pathlib import Path
import zlib
from PIL import Image

RANGES = (150, 100, 50, 20, 5, 1)
WIDTH, HEIGHT = 800, 418


def coordinate(x, y, lat, lon, radius):
    return lat + (209-y)*radius/400/60, (lon + (x-400)*radius/400/60/math.cos(math.radians(lat))+180)%360-180


def mercator(lat, lon, zoom):
    n = 256 * 2**zoom
    return (lon+180)/360*n, (1-math.asinh(math.tan(math.radians(lat)))/math.pi)/2*n


def generate(lat, lon, ranges, tiles, zoom, output, attribution):
    if not math.isfinite(lat) or not math.isfinite(lon) or abs(lat)>80 or abs(lon)>180:
        raise ValueError('center must be finite, latitude within +/-80 and longitude within +/-180')
    if not attribution or len(attribution.encode())>=160 or '\n' in attribution:
        raise ValueError('attribution must fit a single line of fewer than 160 UTF-8 bytes')
    if not ranges or len(set(ranges))!=len(ranges) or any(r not in RANGES for r in ranges):
        raise ValueError('select distinct supported ranges')
    output.mkdir(parents=True, exist_ok=True)
    entries=[]
    for radius in ranges:
        # Cache is scoped to a range; exact inverse projection avoids Mercator stretch/misalignment.
        @lru_cache(maxsize=64)
        def tile(tx,ty):
            with Image.open(tiles/str(zoom)/str(tx)/f'{ty}.png') as image:
                if image.size!=(256,256):raise ValueError('XYZ tiles must be 256x256')
                return image.convert('RGB')
        data=bytearray()
        for y in range(HEIGHT):
            for x in range(WIDTH):
                point=coordinate(x,y,lat,lon,radius)
                if abs(point[0])>=85.05112878:
                    raise ValueError('viewport exceeds Mercator latitude limit')
                px,py=mercator(*point,zoom)
                tx,ty=int(px)//256,int(py)//256
                r,g,b=tile(tx,ty).getpixel((int(px)%256,int(py)%256))
                value=((r>>3)<<11)|((g>>2)<<5)|(b>>3)
                data.extend((value&255,value>>8))
        (output/f'{radius}.rgb565').write_bytes(data)
        entries.append(dict(range_nm=radius, bytes=len(data), crc32=zlib.crc32(data)))
    manifest=dict(version=1,width=WIDTH,height=HEIGHT,projection='local-equirectangular-v1',
                  format='rgb565le',center_lat=lat,center_lon=lon,attribution=attribution,maps=entries)
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2,sort_keys=True)+'\n')
    return manifest


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--lat',type=float,required=True);p.add_argument('--lon',type=float,required=True)
    p.add_argument('--ranges',type=int,nargs='+',default=list(RANGES))
    p.add_argument('--tiles',type=Path,required=True,help='XYZ cache root: z/x/y.png')
    p.add_argument('--zoom',type=int,required=True)
    p.add_argument('--attribution',required=True)
    p.add_argument('--output',type=Path,required=True,help='pack maps directory')
    a=p.parse_args()
    if not 0<=a.zoom<=18:p.error('zoom must be 0..18')
    try:generate(a.lat,a.lon,a.ranges,a.tiles,a.zoom,a.output,a.attribution)
    except (ValueError,OSError) as e:p.error(str(e))

if __name__=='__main__':main()
