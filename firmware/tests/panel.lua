-- Exercise the running application through user inputs; never write panel MMIO.
local frame, failures, checks = 0,0,0
local tests = {
 { 'CHIP31_R0','KY-307 PST 0','padled_ky307_pst0' },
 { 'CHIP31_R3','KY-291 User Menu 1','padled_ky291_usermenu1' },
 { 'CHIP31_R5','KY-291 System Setup','padled_ky291_system_setup' },
 { 'CHIP31_R7','KY-291 Enable Editor','padled_ky291_enable_editor' },
 { 'CHIP11_R1','KY-307 Matte Key1 Edge','padled_ky307_matte_key1edge' },
 { 'CHIP21_R2','KY-307 Key1 On','padled_ky307_key1on' },
 { 'KY306_S0','KY-306 Pad 7','padled_pad7' },
 { 'KY306_S3','KY-306 Pad Enter','padled_padenter' },
 { 'KY308_R0','KY-308 F3','padled_ky308_f3' },
 { 'KY308_R1','KY-308 Bord','padled_ky308_bord' },
 { 'KY308_R2','KY-308 Up','padled_ky308_up' },
}
local function check(ok,label)
 checks=checks+1
 if not ok then failures=failures+1; print('DEMO FAIL '..label) end
end
local function input(port,name,value)
 local p=manager.machine.ioport.ports[':'..port]
 assert(p and p.fields[name], 'Unknown input '..port..' '..name)
 p.fields[name]:set_value(value)
end
local function line(row)
 local lcd=manager.machine.devices[':lcd']
 for name,index in pairs(lcd.items) do
  if name:match('m_ddram$') then return emu.item(index):read_block(row*64,40) end
 end
 error('No LCD DDRAM')
end
local seen={}
local tick_start
emu.register_frame(function()
 frame=frame+1
 local m=manager.machine
 if frame==150 then
  check(line(0):find('T:',1,true)~=nil,'LCD initialized')
  tick_start=m.devices[':maincpu'].spaces['program']:read_u32(clock_address)
 elseif frame==151 then input('KY308_R0','KY-308 F6',1)
 elseif frame==152 then input('KY308_R0','KY-308 F6',0)
 elseif frame==160 then check(m.output:get_value('padled_ky308_f6')==0,'short pulse rejected')
 elseif frame==210 then
  local ticks=m.devices[':maincpu'].spaces['program']:read_u32(clock_address)-tick_start
  print('TIMER ticks per emulated second: '..ticks)
  check(ticks>=998 and ticks<=1002,'1 kHz timer interrupt cadence')
 end
 local n=math.floor((frame-180)/30)+1
 local step=(frame-180)%30
 if n>=1 and n<=#tests then
  local t=tests[n]
  if step==0 then input(t[1],t[2],1)
  elseif step==12 then check(m.output:get_value(t[3])==1,t[3]..' pressed'); input(t[1],t[2],0)
  elseif step==29 then check(m.output:get_value(t[3])==0,t[3]..' released') end
 end
 if frame==540 then
  input('ANALOG_TBAR','T-bar Down (+)',1)
  input('ANALOG_JOY','Joystick X + (Right)',1)
  input('ANALOG_JOY','Joystick Y + (Down)',1)
  for i=1,6 do input('ANALOG_ROTARY','Rotary V'..i..' -',1) end
 elseif frame==541 then
  input('ANALOG_TBAR','T-bar Down (+)',0)
  input('ANALOG_JOY','Joystick X + (Right)',0)
  input('ANALOG_JOY','Joystick Y + (Down)',0)
  for i=1,6 do input('ANALOG_ROTARY','Rotary V'..i..' -',0) end
 elseif frame==590 then
  print('LCD0 '..line(0)); print('LCD1 '..line(1))
  for i=0,5 do check(line(1):sub(3+5*i,6+5*i)=='FFFF','encoder '..i..' negative wrap') end
  for i=1,6 do input('ANALOG_ROTARY','Rotary V'..i..' +',1) end
  local s=m.devices[':maincpu'].spaces['program']
  local t=s:read_u8(0x8000e1)+256*(s:read_u8(0x8000e3)&15)
  check(line(0):sub(3,5)==string.format('%03X',t),'T-bar LCD value')
  check(line(0):sub(9,10)=='7F','joystick X moves')
  check(line(0):sub(14,15)=='7F','joystick Y moves')
  check(t>0,'T-bar moves')
  local patterns={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f}
  for i=5,0,-1 do
   check(m.output:get_value('digit'..i)==patterns[t%10+1],'T-bar seven segment '..i)
   t=math.floor(t/10)
  end
  m.video:snapshot()
  input('KY308_R0','KY-308 F2',1)
 elseif frame==591 then
  for i=1,6 do input('ANALOG_ROTARY','Rotary V'..i..' +',0) end
 elseif frame==595 then input('KY308_R0','KY-308 F2',0)
 elseif frame==650 then
  for i=0,5 do check(line(1):sub(3+5*i,6+5*i)=='0000','encoder '..i..' positive wrap') end
 end
 if frame>=610 and frame<=1050 then
  local state=m.output:get_value('padled_ky308_f3')
  seen[state]=true
  if state==2 and m.output:get_value('digit0')==127 then
   seen.segments=true
   if not seen.snapshot then m.video:snapshot(); seen.snapshot=true end
   if m.output:get_value('digit0_dp')==1 and m.output:get_value('padledauto')==1
      and m.output:get_value('padleddsk')==1 and m.output:get_value('padledftb')==1 then seen.sides=true end
  end
 end
 if frame==1060 then
  check(seen[0] and seen[1] and seen[2],'lamp off and both color planes')
  check(seen.segments,'seven segments all lit')
  check(seen.sides,'decimal point and AUTO/DSK/FTB lamps')
  input('KY308_R0','KY-308 F1',1)
 elseif frame==1065 then input('KY308_R0','KY-308 F1',0)
 elseif frame==1100 then
  check(line(0):sub(1,2)=='T:','return to interactive')
  print(string.format('DEMO %s checks=%d failures=%d', failures==0 and 'PASS' or 'FAIL',checks,failures))
  m:exit()
 end
end)
