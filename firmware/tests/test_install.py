#!/usr/bin/env python3
import json
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from contextlib import redirect_stderr
import io
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from install import main,image_sectors,load_bundle,prepare,apply,replace_candidate,SECTOR
from pack import pack
from sony_monitor import sony_hex,MonitorError,Monitor
ROOT=Path(__file__).resolve().parents[2]

class FakeMonitor:
 def __init__(self):
  # Synthetic identity fixture; never redistribute Sony ROM contents.
  self.eprom=bytes(range(256))*(SECTOR//256)
  self.eprom_prefix=self.eprom[:16]
  self.sectors={n:bytes([n+1])*SECTOR for n in range(8)}
  self.writes=[]
  self.reads=[]
 def read(self,address,size):
  self.reads.append((address,size))
  if address==0x200000: return self.eprom[:size]
  assert size==SECTOR and (address-0x40000)%SECTOR==0
  return self.sectors[(address-0x40000)//SECTOR]
 def program_sector(self,n,data,readback=True): self.writes.append((n,readback)); self.sectors[n]=data

class InstallerTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
  self.bundle=Path(self.temp.name); self.monitor=FakeMonitor()
  identity=patch('install.EPROM_SHA',hashlib.sha256(self.monitor.eprom).hexdigest())
  identity.start(); self.addCleanup(identity.stop)
  self.image=bytes(pack(bytes.fromhex('46fc27004e71'),b'\x4e\x71'))
 def prepare(self): prepare(self.monitor,self.image,self.bundle)
 def test_prepare_disconnect_message(self):
  image=self.bundle/'image.bin'; image.write_bytes(self.image)
  for failure,code in [(MonitorError('serial disconnected'),1),(KeyboardInterrupt(),130)]:
   destination=self.bundle/f'backup-{code}'
   argv=['install.py','prepare','--image',str(image),'--bundle',str(destination),'--port','unused-test-port']
   output=io.StringIO()
   with patch.object(sys,'argv',argv),patch('install.Monitor',side_effect=failure),redirect_stderr(output):
    self.assertEqual(main(),code)
   self.assertIn('No flash was erased or programmed',output.getvalue())
   self.assertNotIn('partially programmed',output.getvalue())
   self.assertFalse((destination/'manifest.json').exists())
 def test_wrong_il_interface_cancelled_before_write(self):
  monitor=Monitor.__new__(Monitor); monitor.write_started=False
  prompt=b'Intel Hex Down Load <Centronics port> down load [10000] ===> flash copy [60000]  (y)/n ? '
  with patch.object(monitor,'send') as send,patch.object(monitor,'wait',side_effect=[prompt,b'Aborted.\n\r\x07>']),patch.object(monitor,'read') as read:
   with self.assertRaisesRegex(MonitorError,'IL cancelled before confirmation'):
    monitor.program_sector(1,b'\xff'*SECTOR)
   self.assertEqual([call.args[0] for call in send.call_args_list],[b'il 10000 60000\r',b'n'])
   self.assertFalse(monitor.write_started); read.assert_not_called()
 def test_sync_drains_startup_before_command(self):
  monitor=Monitor.__new__(Monitor)
  events=[]
  with patch.object(monitor,'settle',side_effect=lambda:events.append('settle')),patch.object(monitor,'command',side_effect=lambda cmd:events.append(('command',cmd))),patch.object(monitor,'read',side_effect=lambda a,n:events.append(('read',a,n))):
   monitor.sync()
  self.assertEqual(events,['settle',('command',''),'settle',('read',0x200000,16)])
 def test_settle_archives_late_prompt(self):
  monitor=Monitor.__new__(Monitor); monitor.fd=123; monitor.pending=bytearray(b'old prompt>')
  def receive(): monitor.pending.extend(b'\r\n>')
  with patch('sony_monitor.select.select',side_effect=[([123],[],[]),([],[],[])]),patch.object(monitor,'_read',side_effect=receive) as read:
   monitor.settle()
  read.assert_called_once(); self.assertFalse(monitor.pending)
 def test_image_validation(self):
  self.assertEqual(image_sectors(self.image),[0,1])
  for offset in [0,SECTOR,SECTOR+4,SECTOR+8,SECTOR+12,SECTOR+16,SECTOR+20,SECTOR+32,7*SECTOR]:
   bad=bytearray(self.image); bad[offset]^=1
   with self.assertRaises(ValueError): image_sectors(bad)
  with self.assertRaises(ValueError): image_sectors(self.image[:-2])
 def test_sony_hex_full_sector(self):
  original=bytes(range(256))*512
  records=sony_hex(original).splitlines(); received=bytearray(); segment=0; extensions=0
  for line in records:
   self.assertTrue(line.startswith(b':'))
   raw=bytes.fromhex(line[1:].decode()); self.assertEqual(sum(raw)&255,0)
   size=raw[0]; address=int.from_bytes(raw[1:3],'big'); kind=raw[3]; data=raw[4:-1]
   self.assertEqual(size,len(data))
   if kind==2: segment=int.from_bytes(data,'big')<<4; extensions+=1
   elif kind==0:
    self.assertEqual(segment+address,len(received)); self.assertEqual(size%2,0)
    for i in range(0,size,2): received+=data[i:i+2][::-1]
   else: self.assertEqual((kind,size,address),(1,0,0))
  self.assertEqual(extensions,1); self.assertEqual(received,original)
  self.assertNotIn(b'\r',sony_hex(original))
  for bad in [b'',b'x',b'xx'*(SECTOR//2+1)]:
   with self.assertRaises(ValueError): sony_hex(bad)
 def test_prepare_install_restore(self):
  original=dict(self.monitor.sectors); self.prepare(); self.assertFalse(self.monitor.writes)
  apply(self.monitor,self.bundle); self.assertEqual(self.monitor.writes,[(1,True),(0,True)])
  apply(self.monitor,self.bundle); self.assertEqual(self.monitor.writes,[(1,True),(0,True)]) # idempotent
  apply(self.monitor,self.bundle,restore=True); self.assertEqual(self.monitor.sectors,original)
  self.assertEqual(self.monitor.writes,[(1,True),(0,True),(1,True),(0,True)]); self.assertNotIn(7,[n for n,_ in self.monitor.writes])
 def test_preflight_all_sectors_before_erase(self):
  self.prepare(); self.monitor.sectors[1]=b'?'*SECTOR
  with self.assertRaises(ValueError): apply(self.monitor,self.bundle)
  self.assertFalse(self.monitor.writes)
  apply(self.monitor,self.bundle,restore=True); self.assertEqual(self.monitor.sectors[1],bytes([2])*SECTOR)
 def test_previous_image_can_be_replaced(self):
  self.prepare(); foreign=b'\x5a'*SECTOR; self.monitor.sectors[1]=foreign
  with self.assertRaises(ValueError): apply(self.monitor,self.bundle)
  self.assertFalse(self.monitor.writes)
  (self.bundle/'previous-1.bin').write_bytes(foreign)
  apply(self.monitor,self.bundle); self.assertEqual(self.monitor.writes,[(1,True),(0,True)])
  self.assertEqual(self.monitor.sectors[1],self.image[SECTOR:2*SECTOR])
 def test_install_image_refreshes_candidate_without_prepare(self):
  self.prepare(); newer=bytes(pack(bytes.fromhex('46fc27004e71'),b'\x4e\x71\x4e\x71'))
  replace_candidate(self.bundle,newer)
  image,_=load_bundle(self.bundle)
  self.assertEqual(image,newer)
  apply(self.monitor,self.bundle)
  self.assertEqual(self.monitor.sectors[1],newer[SECTOR:2*SECTOR])
 def test_fast_skips_dumps_and_overwrites_unknown_flash(self):
  self.prepare(); self.monitor.reads.clear()
  self.monitor.sectors[1]=b'?'*SECTOR
  apply(self.monitor,self.bundle,fast=True)
  self.assertEqual(self.monitor.writes,[(1,False),(0,False)])
  self.assertTrue(all(addr==0x200000 for addr,_ in self.monitor.reads))
  self.assertEqual(self.monitor.sectors[1],self.image[SECTOR:2*SECTOR])
  self.monitor.writes.clear(); self.monitor.reads.clear()
  apply(self.monitor,self.bundle,fast=True)
  self.assertFalse(self.monitor.writes)
 def test_program_sector_fast_skips_readback(self):
  monitor=Monitor.__new__(Monitor); monitor.write_started=False
  prompt=b'Intel Hex Down Load <Serial debug port> down load [10000] ===> flash copy [60000]  (y)/n ? '
  with patch.object(monitor,'send'),patch.object(monitor,'wait',side_effect=[prompt,b'Down load OK\nFlash verify..\nComplete.\n>']),patch.object(monitor,'read') as read:
   monitor.program_sector(1,b'\xff'*SECTOR,readback=False)
  read.assert_not_called()
 def test_bad_bundle_never_accesses_flash(self):
  self.prepare(); path=self.bundle/'original-1.bin'; data=bytearray(path.read_bytes()); data[0]^=1; path.write_bytes(data)
  with self.assertRaises(ValueError): apply(self.monitor,self.bundle)
  self.assertFalse(self.monitor.writes)
 def test_manifest_required_and_sector7_forbidden(self):
  with self.assertRaises(FileNotFoundError): load_bundle(self.bundle)
  self.prepare(); path=self.bundle/'manifest.json'; doc=json.loads(path.read_text()); doc['sectors'][1]['sector']=7; path.write_text(json.dumps(doc))
  with self.assertRaises(ValueError): load_bundle(self.bundle)
 def test_unknown_eprom_no_backup_completion(self):
  self.monitor.eprom=b'\xff'*SECTOR
  with self.assertRaises(ValueError): self.prepare()
  self.assertFalse((self.bundle/'manifest.json').exists()); self.assertFalse(self.monitor.writes)

if __name__=='__main__': unittest.main()
