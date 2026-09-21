-- license:BSD-3-Clause
-- copyright-holders: Salvatore Paxia
-- Exercise the actual input callback and published lamp without changing guest state.
local m=manager.machine
local mode=m.ioport.ports[':bus:console:goino:keyboard:MODIFIERS'].fields['KB Mode']
local g=m.devices[':bus:console:goino']
local frames=0
emu.register_frame_done(function()
 frames=frames+1
 if frames==20 then assert(g:output('keyboard_mode'):get()==0);mode:set_value(1) end
 if frames==25 then
  assert(g:output('keyboard_mode'):get()==1,'Typewriter lamp did not light')
  m.video:snapshot();mode:set_value(0)
 end
 if frames==35 then assert(g:output('keyboard_mode'):get()==1,'Mode failed to latch');mode:set_value(1) end
 if frames==40 then
  assert(g:output('keyboard_mode'):get()==0,'BASIC mode lamp did not go out')
  mode:set_value(0)
  print('PASS: actual keyboard-mode callback toggles and latches lamp; BASIC=off, typewriter=on')
  m:exit()
 end
end)
