#!/usr/bin/env python3
"""Prepare, install and restore BKDS firmware via the unmodified Sony monitor.
prepare is read-only. install/restore require an existing, validated backup bundle.
install --image refreshes the candidate without another prepare dump.
install --fast skips hex dumps and the PC readback; Sony IL verify remains.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import time
import zlib
from sony_monitor import Monitor,MonitorError

SECTOR=0x20000
EPROM_SHA='94b7657c4e3fe36c3206226f38b324cae387128dbc5ab78ab23d80ad060ab91e'

def sha(data): return hashlib.sha256(data).hexdigest()

def image_sectors(image):
    if len(image)!=0x100000: raise ValueError('expected combined 1 MiB flash image')
    magic,version,size,entry,crc,*reserved=struct.unpack_from('>8I',image,SECTOR)
    if magic!=0x424b4131 or version!=1 or entry!=0x60020 or any(reserved): raise ValueError('invalid application header')
    if size<2 or size%2 or size>6*SECTOR-32: raise ValueError('invalid application size')
    if zlib.crc32(image[SECTOR+32:SECTOR+32+size])!=crc: raise ValueError('application CRC mismatch')
    # Known platform entry: MOVE.W #$2700,SR. Reject dumps/injection stubs.
    if image[:4]!=bytes.fromhex('46fc2700'): raise ValueError('unrecognized resident entry')
    end=SECTOR+32+size
    if any(b!=255 for b in image[end:]): raise ValueError('unexpected data after application / reserved sector')
    return list(range((end+SECTOR-1)//SECTOR))

def durable(path,data):
    with path.open('xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    fd=os.open(path.parent,os.O_RDONLY|os.O_DIRECTORY)
    try: os.fsync(fd)
    finally: os.close(fd)

def durable_replace(path,data):
    tmp=path.parent/f'.{path.name}.{time.time_ns()}.tmp'
    with tmp.open('xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.replace(tmp,path)
    fd=os.open(path.parent,os.O_RDONLY|os.O_DIRECTORY)
    try: os.fsync(fd)
    finally: os.close(fd)

def journal(bundle,message):
    with (bundle/'operations.log').open('a') as f:
        f.write(time.strftime('%Y-%m-%dT%H:%M:%S%z')+' '+message+'\n'); f.flush(); os.fsync(f.fileno())
    print(message,flush=True)

def check_eprom(monitor,bundle=None,full=True):
    if full:
        print('Checking Sony EPROM (read-only)...',flush=True)
        data=monitor.read(0x200000,SECTOR)
        if sha(data)!=EPROM_SHA: raise ValueError('unknown Sony EPROM: refusing monitor-specific writes')
        return data
    prefix=getattr(monitor,'eprom_prefix',None) or monitor.read(0x200000,16)
    expected=(bundle/'eprom.bin').read_bytes()[:16]
    if prefix!=expected:
        raise ValueError('EPROM prefix mismatch; this is not the backed-up desk')
    return prefix

def prepare(monitor,image,bundle):
    sectors=image_sectors(image)
    eprom=check_eprom(monitor)
    durable(bundle/'eprom.bin',eprom); durable(bundle/'candidate.bin',image)
    entries=[]
    for sector in sectors:
        print(f'Backing up complete sector {sector}...',flush=True)
        original=monitor.read(0x40000+sector*SECTOR,SECTOR)
        durable(bundle/f'original-{sector}.bin',original)
        entries.append({'sector':sector,'original_sha256':sha(original),'candidate_sha256':sha(image[sector*SECTOR:(sector+1)*SECTOR])})
    manifest={'version':1,'eprom_sha256':EPROM_SHA,'image_sha256':sha(image),'sectors':entries}
    # Manifest is the completion marker. A partial backup cannot be installed.
    durable(bundle/'manifest.json',(json.dumps(manifest,indent=2)+'\n').encode())
    journal(bundle,'PREPARED: no flash written; sectors '+','.join(map(str,sectors))+'; sector 7 and EPROM excluded')
    return manifest

def load_bundle(bundle):
    manifest=json.loads((bundle/'manifest.json').read_text())
    image=(bundle/'candidate.bin').read_bytes(); sectors=image_sectors(image)
    if manifest.get('version')!=1 or manifest.get('eprom_sha256')!=EPROM_SHA: raise ValueError('invalid bundle version/EPROM')
    if sha((bundle/'eprom.bin').read_bytes())!=EPROM_SHA or sha(image)!=manifest.get('image_sha256'): raise ValueError('bundle image hash mismatch')
    entries=manifest.get('sectors',[])
    if [e.get('sector') for e in entries]!=sectors: raise ValueError('invalid sector list')
    original={}
    for e in entries:
        n=e['sector']; data=(bundle/f'original-{n}.bin').read_bytes()
        if len(data)!=SECTOR or sha(data)!=e.get('original_sha256'): raise ValueError(f'backup sector {n} hash mismatch')
        if sha(image[n*SECTOR:(n+1)*SECTOR])!=e.get('candidate_sha256'): raise ValueError(f'candidate sector {n} hash mismatch')
        original[n]=data
    return image,original

def replace_candidate(bundle,image):
    _,original=load_bundle(bundle)
    sectors=image_sectors(image)
    if sectors!=list(original):
        raise ValueError('new image spans sectors not in this backup; run prepare')
    durable_replace(bundle/'candidate.bin',image)
    entries=[{'sector':n,'original_sha256':sha(original[n]),
              'candidate_sha256':sha(image[n*SECTOR:(n+1)*SECTOR])} for n in sectors]
    manifest={'version':1,'eprom_sha256':EPROM_SHA,'image_sha256':sha(image),'sectors':entries}
    durable_replace(bundle/'manifest.json',(json.dumps(manifest,indent=2)+'\n').encode())
    journal(bundle,'CANDIDATE updated; originals and EPROM backup unchanged; sha256='+sha(image))
    return image,original

def load_last(bundle):
    path=bundle/'last-installed.json'
    if not path.exists(): return {}
    doc=json.loads(path.read_text())
    return {int(k):v for k,v in doc.items()}

def save_last(bundle,last):
    durable_replace(bundle/'last-installed.json',(json.dumps({str(k):v for k,v in sorted(last.items())},indent=2)+'\n').encode())

def apply(monitor,bundle,restore=False,fast=False):
    image,original=load_bundle(bundle)
    check_eprom(monitor,bundle,full=not fast)
    targets={n:(data if restore else image[n*SECTOR:(n+1)*SECTOR]) for n,data in original.items()}
    last=load_last(bundle)
    current={}
    if fast:
        journal(bundle,'INSTALL FAST: EPROM peek only; no preflight dump; no PC readback; Sony IL verify remains')
    else:
        # Check EVERY sector before the first erase. A different device/image or a
        # partial prior programming attempt requires explicit restore, not blind retry.
        for n in original:
            print(f'Preflight sector {n}...',flush=True)
            current[n]=monitor.read(0x40000+n*SECTOR,SECTOR)
            allowed=[original[n],targets[n]]
            previous=bundle/f'previous-{n}.bin'
            if previous.exists():
                data=previous.read_bytes()
                if len(data)!=SECTOR: raise ValueError(f'previous sector {n} has the wrong size')
                allowed.append(data)
            if not restore and current[n] not in allowed:
                raise ValueError(f'sector {n} differs from backup, previous and target; use restore for interrupted installation')
    mode='RESTORE' if restore else ('INSTALL FAST' if fast else 'INSTALL')
    for n in [i for i in targets if i!=0]+[0]:
        if not fast and current[n]==targets[n]:
            journal(bundle,f'{mode} sector {n}: already matches; skipped')
            last[n]=sha(targets[n]); save_last(bundle,last); continue
        if fast and last.get(n)==sha(targets[n]):
            journal(bundle,f'{mode} sector {n}: matches last successful IL; skipped')
            continue
        journal(bundle,f'{mode} sector {n}: REQUEST IL (awaiting confirmation); target sha256={sha(targets[n])}')
        monitor.program_sector(n,targets[n],readback=not fast)
        last[n]=sha(targets[n]); save_last(bundle,last)
        journal(bundle,f'{mode} sector {n}: '+('Sony IL verify only' if fast else 'VERIFIED full readback'))
    journal(bundle,f'{mode} COMPLETE. Remain in monitor; change DS0 to OFF and power-cycle for autonomous boot.')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['prepare','inspect','install','restore'])
    parser.add_argument('--bundle',type=Path,required=True)
    parser.add_argument('--port',help='SCI2 serial device at the Sony debug prompt, 9600 8N1')
    parser.add_argument('--image',type=Path,help='combined flash-link.bin; required for prepare, optional for install')
    parser.add_argument('--fast',action='store_true',help='install without hex dumps or PC readback; requires an existing backup bundle')
    args=parser.parse_args()
    monitor=None
    try:
        if args.fast and args.action!='install': parser.error('--fast is only valid with install')
        if args.action=='inspect':
            image,original=load_bundle(args.bundle)
            print(json.dumps({'candidate_sha256':sha(image),'write_order':[n for n in original if n]+[0],
                'backup_bytes':len(original)*SECTOR,'protected':['Sony EPROM','flash sector 7','all unlisted sectors']},indent=2)); return 0
        if not args.port: parser.error('--port is required')
        if args.action=='prepare':
            if not args.image: parser.error('--image is required')
            image=args.image.read_bytes(); image_sectors(image)
            args.bundle.mkdir(parents=True,exist_ok=False)
            parent_fd=os.open(args.bundle.parent,os.O_RDONLY|os.O_DIRECTORY)
            try: os.fsync(parent_fd)
            finally: os.close(parent_fd)
        else:
            if args.action=='install' and args.image:
                replace_candidate(args.bundle,args.image.read_bytes())
            load_bundle(args.bundle) # no serial access until local files are valid
        monitor=Monitor(args.port,args.bundle/f'{args.action}-{time.time_ns()}.serial.log')
        try:
            monitor.sync()
            if args.action=='prepare': prepare(monitor,image,args.bundle)
            else: apply(monitor,args.bundle,restore=args.action=='restore',fast=args.fast)
        finally: monitor.close()
        return 0
    except (OSError,ValueError,KeyError,TypeError,MonitorError) as error:
        advice=('No flash was erased or programmed. An incomplete prepare bundle cannot be installed; retry prepare in a new directory after fixing the connection.'
                if args.action in ('prepare','inspect') else
                'No flash was erased or programmed by this invocation; return to the monitor prompt before retrying.'
                if monitor is None or not getattr(monitor,'write_started',True) else
                'Do not boot a partially programmed image; keep debug mode for recovery.')
        print(f'STOPPED: {error}. {advice}',file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        advice=('No flash was erased or programmed; retry prepare in a new directory.'
                if args.action in ('prepare','inspect') else
                'Keep debug mode active; use the completed backup bundle to restore.')
        print(f'Interrupted. {advice}',file=sys.stderr); return 130

if __name__=='__main__': sys.exit(main())
