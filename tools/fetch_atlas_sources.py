#!/usr/bin/env python3
"""Fetch or verify the exact source snapshots in atlas-source-lock.json.

Keep the verified source cache: GeoNames publishes rolling daily dumps. A newer
dump deliberately fails the hash check until the lock is explicitly refreshed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.request

def sha256(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1048576),b''):h.update(chunk)
    return h.hexdigest()

def fetch(destination,lock):
    destination.mkdir(parents=True,exist_ok=True)
    for name,entry in json.loads(lock.read_text()).items():
        if Path(name).name!=name:raise ValueError('unsafe source filename')
        path=destination/name
        if path.exists() and sha256(path)==entry['sha256']:
            print('Verified',name,flush=True);continue
        temporary=destination/(name+'.download')
        try:
            size=0
            with urllib.request.urlopen(entry['url'],timeout=60) as response,temporary.open('wb') as f:
                while chunk:=response.read(1048576):
                    size+=len(chunk)
                    if size>536870912:raise ValueError('source exceeds download bound')
                    f.write(chunk)
            if sha256(temporary)!=entry['sha256']:
                raise ValueError(f'{name}: source changed. Restore the retained snapshot or explicitly update the source lock before rebuilding.')
            temporary.replace(path);print('Downloaded and verified',name,flush=True)
        finally:
            temporary.unlink(missing_ok=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--lock',type=Path,default=Path(__file__).with_name('atlas-source-lock.json'))
    args=parser.parse_args();fetch(args.output,args.lock)
