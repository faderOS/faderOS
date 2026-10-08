#!/usr/bin/env python3
import ctypes
import io
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'firmware/tools'),str(ROOT/'controller/python')]
from self_update import Host,UpdateError
from protocol import encode,decode
from pack import pack

class ProtocolTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory(); cls.lib=Path(cls.tmp.name)/'protocol.so'
  subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-shared','-fPIC','-I'+str(ROOT/'firmware/include'),str(ROOT/'firmware/tests/update_protocol_native.c'),str(ROOT/'firmware/common/image.c'),'-o',str(cls.lib)],check=True)
  cls.native=ctypes.CDLL(str(cls.lib))
 @classmethod
 def tearDownClass(cls): cls.tmp.cleanup()
 def setUp(self): self.native.test_reset()
 def exchange(self,packet):
  output=ctypes.create_string_buffer(4096)
  size=self.native.test_exchange(packet,len(packet),output)
  return [decode(frame) for frame in output.raw[:size].split(b'\0') if frame]
 def test_hello_and_duplicate_commit(self):
  frames=self.exchange(encode(1,0,123))
  self.assertEqual(frames[0][:3],(0x91,0,123)); self.assertEqual(len(frames[0][3]),12)
  self.assertEqual(self.native.test_resets(),1)
  self.exchange(encode(1,0,123)); self.assertEqual(self.native.test_resets(),1)
  packet=encode(0x43,1,123,b'\1')
  a=self.exchange(packet); b=self.exchange(packet)
  self.assertEqual(a,b); self.assertEqual(a[0][3],b'\0'); self.assertEqual(self.native.test_commands(),1)
  self.assertEqual(self.exchange(encode(0x43,1,123,b'\0'))[0][3],b'\5')
  self.assertEqual(self.native.test_commands(),1)
 def test_crc_session_and_reconnect(self):
  self.exchange(encode(1,0,123))
  packet=bytearray(encode(0x40,1,123,b'abc'));packet[-3]^=1
  self.assertEqual(self.exchange(bytes(packet)),[]);self.assertEqual(self.native.test_commands(),0)
  self.assertEqual(self.exchange(encode(0x40,1,124)),[])
  self.exchange(encode(1,0,124));self.assertEqual(self.native.test_resets(),2)
  self.exchange(encode(0x40,1,124));self.assertEqual(self.native.test_commands(),1)
 def test_partial_overlong_delimiter(self):
  packet=encode(1,0,123)
  self.assertEqual(self.exchange(packet[:7]),[])
  self.assertEqual(self.exchange(packet[7:])[0][0],0x91)
  self.assertEqual(self.exchange(b'a'*220+b'\0'),[])
  self.assertEqual(self.exchange(packet)[0][0],0x91)
 def test_client_plan_order_and_ram_only(self):
  image=bytes(pack(bytes.fromhex('46fc2700'),b'\x4e\x71'))
  h=Host.__new__(Host)
  with patch.object(h,'command') as command:
   h.install(image,boot=False)
   commits=[c.args[1][0] for c in command.call_args_list if c.args[0]==0x43]
   self.assertEqual(commits,[1,0]);self.assertEqual(command.call_args_list[-1].args[0],0x44)
  with patch.object(h,'command') as command:
   h.install(image,stage_only=True)
   kinds=[c.args[0] for c in command.call_args_list]
   self.assertIn(0x46,kinds);self.assertNotIn(0x43,kinds);self.assertNotIn(0x45,kinds)
 def test_ram_link_map_and_no_external_symbols(self):
  elf=ROOT/'firmware/build/update.elf'
  self.assertEqual(subprocess.check_output(['m68k-elf-nm','-u',str(elf)]),b'')
  symbols=subprocess.check_output(['m68k-elf-nm','-n',str(elf)],text=True)
  addresses={p[2]:int(p[0],16) for line in symbols.splitlines() if len(p:=line.split())==3}
  self.assertEqual(addresses['update_start'],0x31000)
  self.assertLessEqual(addresses['__bss_end'],0x3c000)
  for address in addresses.values(): self.assertTrue(0x31000<=address<=0x3c000)

if __name__=='__main__': unittest.main()
