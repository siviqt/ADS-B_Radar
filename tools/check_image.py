#!/usr/bin/env python3
"""Conservative phase-1 gate: retain the launcher's real 2 MiB OTA size check."""
import argparse
import hashlib
import json
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('image', type=Path)
p.add_argument('--free-bytes', type=int, help='Fresh AppFS free-space reading, not the phase-0 snapshot')
a = p.parse_args()
data = a.image.read_bytes()
size = len(data)
allocated = ((size + 65535) // 65536) * 65536
if not data or data[0] != 0xE9:
    p.error('not an ESP application image')
if size > 2 * 1024 * 1024:
    p.error('phase-1 image exceeds the unchanged 2 MiB reference OTA limit')
if allocated > 8128 * 1024:
    p.error('image exceeds reference usable AppFS capacity')
if a.free_bytes is not None and allocated > a.free_bytes:
    p.error('insufficient current AppFS free space; use launcher cache management')
record = dict(image=a.image.name, bytes=size, appfs_allocation_bytes=allocated,
              sha256=hashlib.sha256(data).hexdigest(), ota_limit_bytes=2097152,
              current_free_bytes=a.free_bytes)
a.image.with_suffix('.appfs.json').write_text(json.dumps(record, indent=2) + '\n')
print(json.dumps(record, indent=2))
