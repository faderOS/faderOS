"""Isolated MAME instance for exercising the real Sony monitor through SCI2."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time
ROOT=Path(__file__).resolve().parents[2]
class Machine:
 def __init__(self):
  self.work=Path(tempfile.mkdtemp(prefix='bkds-install-test-'))
  self.proc=None
 def start(self,debug=True):
  cfg=self.work/'cfg'; cfg.mkdir(exist_ok=True)
  dip=0xfc if debug else 0xff
  ports=''.join(f'<port tag=":DSW1" type="DIPSWITCH" mask="{1<<b}" defvalue="{1<<b}" value="{dip&(1<<b)}" />' for b in range(8))
  (cfg/'bkds2010.cfg').write_text(f'<mameconfig version="10"><system name="bkds2010"><input>{ports}</input></system></mameconfig>')
  (self.work/'quit').unlink(missing_ok=True)
  (self.work/'pc').unlink(missing_ok=True)
  (self.work/'control.lua').write_text("local n=0; emu.register_frame(function() local f=io.open('quit','r'); if f then f:close(); manager.machine:exit() end; n=n+1; if n%60==0 then local p=io.open('pc','w'); p:write(string.format('%x',manager.machine.devices[':maincpu'].state['PC'].value)); p:close() end end)")
  self.log=(self.work/'mame.log').open('wb')
  self.proc=subprocess.Popen(['stdbuf','-oL',str(ROOT/'mame/mame'),'bkds2010','-bios','original','-noreadconfig','-rompath',str(ROOT/'mame/roms'),'-homepath',str(self.work),'-cfg_directory',str(cfg),'-nvram_directory',str(self.work/'nvram'),'-video','none','-sound','none','-skip_gameinfo','-nothrottle','-frameskip','10','-autoboot_delay','0','-autoboot_script',str(self.work/'control.lua')],cwd=self.work,stdout=self.log,stderr=subprocess.STDOUT,env={**os.environ,'SDL_VIDEODRIVER':'dummy','SDL_AUDIODRIVER':'dummy'})
  end=time.monotonic()+10
  while time.monotonic()<end:
   paths=re.findall(rb'PSEUDO_TERMINAL: Opened at (\S+)',(self.work/'mame.log').read_bytes())
   if len(paths)>=2: return paths[1].decode()
   if self.proc.poll() is not None: raise RuntimeError((self.work/'mame.log').read_text())
   time.sleep(.02)
  raise TimeoutError('MAME PTY')
 def stop(self):
  if self.proc and self.proc.poll() is None:
   (self.work/'quit').touch()
   try: self.proc.wait(timeout=10)
   except subprocess.TimeoutExpired: self.proc.kill(); self.proc.wait()
  if self.proc: self.log.close()
 def flash(self):
  nv=self.work/'nvram/bkds2010'
  hi=(nv/'flash_hi').read_bytes(); lo=(nv/'flash_lo').read_bytes()
  result=bytearray(len(hi)+len(lo)); result[::2]=hi; result[1::2]=lo
  return bytes(result)
