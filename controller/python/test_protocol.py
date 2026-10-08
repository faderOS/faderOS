import os
import random
from pathlib import Path
import select
import subprocess
import sys
import time
import struct
import unittest
from protocol import Client,TransportClosed,encode,decode
from panel_logic import Blinker,Inputs

class ProtocolTests(unittest.TestCase):
    def test_roundtrip_all_payload_lengths(self):
        rng=random.Random(68301)
        for n in range(193):
            p=bytes(rng.randrange(256) for _ in range(n))
            wire=encode(0x10,123,0xabcdef12,p)
            self.assertNotIn(0,wire[:-1])
            self.assertEqual(decode(wire[:-1]),(0x10,123,0xabcdef12,p))
        for p in (b'\0'*192,b'\xff'*192):
            self.assertEqual(decode(encode(1,0,1,p)[:-1])[-1],p)

    def test_corruption(self):
        p=bytearray(encode(0x10,1,42,b'hello'))
        p[-3]^=1
        with self.assertRaises(ValueError): decode(p[:-1])
        for p in (b'',b'\0',b'\xffshort'):
            with self.assertRaises(ValueError): decode(p)
        with self.assertRaises(ValueError): encode(1,1,1,b'x'*193)

    def test_closed_pty_read_and_write(self):
        for operation in ('read','write'):
            with self.subTest(operation=operation):
                master,slave=os.openpty()
                client=Client(os.ttyname(slave))
                os.close(master)
                try:
                    with self.assertRaises(TransportClosed):
                        if operation=='read': client.poll(.2)
                        else: client.write(b'heartbeat')
                finally:
                    client.close(); os.close(slave)

    def test_host_exits_cleanly_when_heartbeat_port_disappears(self):
        master,slave=os.openpty()
        process=subprocess.Popen([sys.executable,str(Path(__file__).with_name('host.py')),os.ttyname(slave)],
                                 stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        pending=bytearray(); heartbeat=False
        try:
            deadline=time.monotonic()+5
            while not heartbeat and time.monotonic()<deadline:
                if not select.select([master],[],[],.1)[0]: continue
                pending.extend(os.read(master,4096))
                while 0 in pending:
                    i=pending.index(0); frame=bytes(pending[:i]); del pending[:i+1]
                    if not frame: continue
                    kind,seq,session,payload=decode(frame)
                    if kind==1:
                        os.write(master,encode(0x81,0,session,b'\0\1\0\x0f'+bytes(55)))
                    elif kind==2:
                        heartbeat=True
            self.assertTrue(heartbeat,'host never started heartbeat')
            os.close(master); master=None
            # Keep stdin open until the process exits: it must detect the PTY loss.
            process.wait(timeout=3)
            output=process.communicate()[0].decode()
            self.assertEqual(process.returncode,0,output)
            self.assertIn('SCI2 disconnected; host stopped',output)
            self.assertNotIn('Traceback',output)
        finally:
            if process.poll() is None: process.kill(); process.communicate()
            if master is not None: os.close(master)
            os.close(slave)

    def test_duplicate_events_and_session_filter(self):
        master,slave=os.openpty(); states=[]
        client=Client(os.ttyname(slave),lambda s,r:states.append(s))
        try:
            client.session=42; client.ready=True
            payload=struct.pack('>I',100)+bytes([1])+bytes(22)+struct.pack('>HBB6I',15,128,128,*([0]*6))
            frame=encode(0x82,1,42,payload)
            os.write(master,frame); client.poll(.2)
            os.write(master,frame); client.poll(.2)
            self.assertEqual(len(states),1)
            self.assertEqual(states[0]['buttons'],{0})
            os.write(master,encode(0x82,2,41,payload)); client.poll(.2)
            self.assertEqual(len(states),1)
            os.write(master,encode(0x82,3,42,payload))
            with self.assertRaises(ConnectionError): client.poll(.2)
        finally:
            client.close(); os.close(master); os.close(slave)

    def test_no_gestures_from_pre_handshake_snapshots(self):
        master,slave=os.openpty(); states=[]
        client=Client(os.ttyname(slave),lambda s,r:states.append(s))
        try:
            client.session=42
            payload=struct.pack('>I',100)+bytes(51)
            os.write(master,encode(0x82,1,42,payload)); client.poll(.2)
            self.assertEqual(states,[])
            client.ready=True
            os.write(master,encode(0x82,2,42,payload)); client.poll(.2)
            self.assertEqual(len(states),1)
            client.event=65535
            os.write(master,encode(0x82,1,42,payload)); client.poll(.2)
            self.assertEqual(len(states),2)
        finally:
            client.close(); os.close(master); os.close(slave)

class LogicTests(unittest.TestCase):
    def test_blink(self):
        b=Blinker(); b.set(1,1,1000); b.set(2,2,1000)
        self.assertEqual(b.update(0),[(1,0),(2,0)])
        self.assertEqual(b.update(499),[])
        self.assertEqual(b.update(500),[(1,1),(2,2)])
        self.assertEqual(b.update(1000),[(1,0),(2,0)])
        b.remove(1); b.clear(); self.assertEqual(b.update(1500),[])

    def test_multibutton_double_click_and_encoder_wrap(self):
        p=Inputs()
        def sample(ms,keys,rot=0,sync=False):
            return p.update(dict(ms=ms,buttons=set(keys),rotary=(rot,0,0,0,0,0)),sync)
        sample(0,[],sync=True)
        self.assertEqual(sample(10,[1,2])['down'],{1,2})
        self.assertEqual(sample(50,[])['up'],{1,2})
        self.assertEqual(sample(200,[1,2])['double'],[1,2])
        self.assertEqual(sample(250,[1,2],0xffffffff)['delta'][0],-1)
        self.assertEqual(sample(300,[1,2],0)['delta'][0],1)
        sample(400,[1,2],sync=True)
        self.assertEqual(sample(401,[1,2])['down'],set())
        sample(450,[])
        self.assertEqual(sample(500,[1])['double'],[])

if __name__=='__main__': unittest.main()
