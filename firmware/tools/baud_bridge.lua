-- Explicit control of the emulated Pico UART. Never inspect MCU registers to
-- guess its speed. The host requests a baud and gets an acknowledgement only
-- after both PTY endpoints have adopted it. Files live in the isolated cwd.
local last=''
local pending=nil
local age=0
local rates={[9600]=0x07,[19200]=0x09,[38400]=0x0b}
emu.register_frame(function()
 if pending then
  age=age+1
  if age>=2 then
   local f=io.open('baud-ack.tmp','w')
   if f then f:write(pending); f:close(); os.rename('baud-ack.tmp','baud-ack') end
   pending=nil
  end
 end
 local f=io.open('baud-request','r'); if not f then return end
 local line=f:read('*a'); f:close()
 if line==last then return end
 local seq,baud=line:match('^(%d+) (%d+)')
 baud=tonumber(baud)
 if not seq or not rates[baud] then return end
 local ports=manager.machine.ioport.ports
 local tx=ports[':serial2:pty:RS232_TXBAUD']
 local rx=ports[':serial2:pty:RS232_RXBAUD']
 if not tx or not rx then return end
 tx.fields['TX Baud'].user_value=rates[baud]
 rx.fields['RX Baud'].user_value=rates[baud]
 print('PICO UART BAUD '..baud)
 last=line; pending=line; age=0
end)
