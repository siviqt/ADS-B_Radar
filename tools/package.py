#!/usr/bin/env python3
"""Assemble and validate a deterministic Tanmatsu AppFS package (no device writes)."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import zlib
from PIL import Image
import jsonschema
import yaml

ROOT=Path(__file__).resolve().parents[1]
DEFAULT_SCHEMA=ROOT/'tools/package.schema.json'


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def resolved_bsp_major(lockfile):
    resolved=yaml.safe_load(lockfile.read_text())
    version=resolved['dependencies']['badgeteam/badge-bsp']['version']
    parts=str(version).split('.')
    if len(parts)!=3 or not all(p.isdecimal() for p in parts) or int(parts[0])<1:
        raise ValueError(f'invalid resolved BSP version: {version}')
    return int(parts[0])


def validate(output,schema,expected_bsp_major=None):
    metadata=json.loads((output/'metadata.json').read_text())
    jsonschema.validate(metadata,json.loads(schema.read_text()))
    def file(name):
        p=output/name
        if Path(name).is_absolute() or '..' in Path(name).parts or not p.is_file():
            raise ValueError(f'invalid or missing package file: {name}')
        return p
    for key,name in metadata['icon'].items():
        with Image.open(file(name)) as im:
            size=int(key.split('x')[0])
            if im.size!=(size,size) or im.format!='PNG':raise ValueError('incorrect icon size/format')
    file(metadata['license_file'])
    for app in metadata['application']:
        if app['targets']!=['tanmatsu'] or app['type']!='appfs' or app['revision']<1:raise ValueError('unsupported target/revision')
        if expected_bsp_major is not None and app.get('bsp_version')!=expected_bsp_major:
            raise ValueError('package BSP major does not match the resolved build')
        image=file(app['executable']).read_bytes()
        if not image or image[0]!=0xe9 or len(image)>2097152:raise ValueError('invalid or oversized ESP image')
        for asset in app.get('assets',[]):
            file(asset['source_file'])
            if Path(asset['target_file']).is_absolute() or '..' in Path(asset['target_file']).parts:raise ValueError('unsafe asset destination')
    for app in metadata['application']:
        assets={a['target_file']:a['source_file'] for a in app.get('assets',[])}
        if 'atlas-v2.json' in assets:
            if not app.get('external_only'):raise ValueError('worldwide assets require SD installation')
            manifest=json.loads(file(assets['atlas-v2.json']).read_text())
            if manifest.get('version')!=2 or manifest.get('language')!='en' or manifest.get('coverage')!=[-180,-90,180,90]:raise ValueError('invalid atlas manifest')
            required={'world-v2.idx','world-v2-000.avp','cities-v1.idx','cities-v1.dat','DATA_NOTICES.txt'}
            if not required.issubset(manifest.get('files',{})):raise ValueError('incomplete atlas')
            for name,entry in manifest['files'].items():
                if name not in assets:raise ValueError('atlas asset missing from install list')
                source=file(assets[name])
                if source.stat().st_size!=entry['bytes'] or digest(source)!=entry['sha256'] or zlib.crc32(source.read_bytes())!=entry.get('crc32'):raise ValueError('atlas size/hash mismatch')
    return metadata


def assemble(image,output,schema,revision,version,icons,records,atlas=None):
    build=json.loads((ROOT/'build/project_description.json').read_text())
    if build['project_version']!=version:
        raise ValueError('package version does not match the built firmware')
    bsp_major=resolved_bsp_major(ROOT/'dependencies.lock')
    # Preserve any previous package and its exact executable before replacing files.
    if (output/'metadata.json').exists():
        old=json.loads((output/'metadata.json').read_text())
        old_revision=max(a['revision'] for a in old['application'])
        if revision<=old_revision:raise ValueError('revision must increase over the existing package')
        rollback=records/f'rollback-r{old_revision}'
        if rollback.exists():
            if any(not (output/p.relative_to(rollback)).is_file() or digest(output/p.relative_to(rollback))!=digest(p) for p in rollback.rglob('*') if p.is_file()):
                raise ValueError('existing rollback differs; choose another records directory')
        else:shutil.copytree(output,rollback,ignore=shutil.ignore_patterns('.git','.agents','.aws','.codex'))
    destination=output
    staging=tempfile.TemporaryDirectory(prefix="adsb-package-")
    output=Path(staging.name)
    records.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(image,output/'application.bin')
    shutil.copyfile(ROOT/'LICENSE',output/'LICENSE')
    for size in (16,32,64):
        source=icons/f'icon{size}.png'
        if source.resolve()!=(output/f'icon{size}.png').resolve():shutil.copyfile(source,output/f'icon{size}.png')
    atlas=atlas or ROOT/'assets/world'
    atlas_manifest=json.loads((atlas/'atlas-v2.json').read_text())
    asset_names=['atlas-v2.json',*sorted(atlas_manifest['files'])]
    for name in asset_names:
        if Path(name).name!=name:raise ValueError('atlas filenames must be flat')
        shutil.copyfile(atlas/name,output/name)
    # Retain all component license texts and bundled third-party license files.
    notices=['ADS-B Radar for Tanmatsu\nUpstream: https://github.com/iamneilroberts/adsb\nCopyright (c) 2026 Neil; MIT (see LICENSE).\nTanmatsu port: ADS-B Radar contributors (see LICENSE).\nWorldwide SD atlas: Natural Earth (public domain). English city index: GeoNames (CC BY 4.0). See DATA_NOTICES.txt.\n']
    roots=[ROOT/'managed_components',ROOT/'components',ROOT/'compat']
    idf=Path(build['idf_path'])
    notices.append((ROOT/'tools/licenses/BUILTIN_FONTS.txt').read_text())
    for base in roots+[idf]:
        search=base/'components' if base==idf else base
        paths=sorted(p for p in search.rglob('*') if p.is_file() and p.name.lower().startswith(('license','licence','copying','notice','ofl')))
        if base==idf:paths.insert(0,idf/'LICENSE')
        for path in paths:
            if path.is_file() and path.stat().st_size<1000000:
                notices.append(f'\n--- {base.name}/{path.relative_to(base)} ---\n'+path.read_text(errors='replace'))
    (output/'THIRD_PARTY_NOTICES.txt').write_text('\n'.join(notices))
    metadata=dict(name='ADS-B Radar',description='Aircraft map and radar with arrivals and statistics',categories=['Tools'],
                  version=version,icon={f'{s}x{s}':f'icon{s}.png' for s in (16,32,64)},author='ADS-B Radar contributors; upstream Neil',
                  license_type='MIT',license_file='LICENSE',application=[dict(targets=['tanmatsu'],revision=revision,bsp_version=bsp_major,type='appfs',
                  executable='application.bin',external_only=True,assets=[dict(source_file=f,target_file=f) for f in ['LICENSE','THIRD_PARTY_NOTICES.txt',*asset_names]])])
    (output/'metadata.json').write_text(json.dumps(metadata,indent=2)+'\n')
    validate(output,schema,expected_bsp_major=bsp_major)
    files=['application.bin','metadata.json','LICENSE','THIRD_PARTY_NOTICES.txt','icon16.png','icon32.png','icon64.png',*asset_names]
    checks={f:digest(output/f) for f in files}
    (output/'SHA256SUMS').write_text(''.join(f'{checks[f]}  {f}\n' for f in sorted(checks)))
    for f in ('adsb_bringup.elf','adsb_bringup.map','project_description.json'):
        shutil.copyfile(ROOT/'build'/f,records/f)
    for f in ('dependencies.lock','sdkconfig','sdkconfig.defaults'):
        shutil.copyfile(ROOT/f,records/f)
    source_files={p for name in ('main','tools','tests','components','compat') for p in (ROOT/name).rglob('*')
                  if p.is_file() and '__pycache__' not in p.parts}
    source_files.update(ROOT/name for name in ('CMakeLists.txt','build.sh','LICENSE','dependencies.lock','sdkconfig.defaults','partitions.csv'))
    sources={str(p.relative_to(ROOT)):digest(p) for p in sorted(source_files)}
    record=dict(revision=revision,version=version,bsp_version=bsp_major,package_sha256=checks,source_sha256=sources,
                image_bytes=image.stat().st_size,appfs_allocation_bytes=(image.stat().st_size+65535)//65536*65536,
                hardware_acceptance='pending',reproducibility='deterministic packaging for identical input files; firmware reproducibility tested separately')
    (records/'release.json').write_text(json.dumps(record,indent=2,sort_keys=True)+'\n')
    destination.mkdir(parents=True,exist_ok=True)
    for path in output.iterdir():shutil.copyfile(path,destination/path.name)
    # Previous executable is preserved in rollback records.
    if (destination/'adsb.bin').exists() and (destination/'adsb.bin').resolve()!=image.resolve():
        (destination/'adsb.bin').unlink()
    staging.cleanup()
    return record


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--schema',type=Path,default=DEFAULT_SCHEMA)
    p.add_argument('--image',type=Path,default=ROOT/'build/adsb_bringup.bin')
    p.add_argument('--icons',type=Path);p.add_argument('--records',type=Path)
    p.add_argument('--atlas',type=Path,default=ROOT/'assets/world');p.add_argument('--revision',type=int);p.add_argument('--version',default='0.3.2');p.add_argument('--validate-only',action='store_true')
    a=p.parse_args()
    if a.validate_only:
        validate(a.output,a.schema,expected_bsp_major=resolved_bsp_major(ROOT/'dependencies.lock'))
        print('Package schema, resolved BSP major and referenced files pass');return
    if not all((a.icons,a.records,a.revision)):p.error('--icons, --records and --revision are required to assemble')
    print(json.dumps(assemble(a.image,a.output,a.schema,a.revision,a.version,a.icons,a.records,a.atlas),indent=2))

if __name__=='__main__':main()
