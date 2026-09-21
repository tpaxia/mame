-- license:BSD-3-Clause
-- Requires generated two-HD ESE, SYSBTS in FD2 and FD1 empty; use disposable images.
-- copyright-holders: Salvatore Paxia
local m=manager.machine
local seen={{},{},{},{}}
local snapped=false
emu.register_frame_done(function()
 for i=0,3 do
  local v=m.output:get_value('disk_activity'..i)
  if v~=0 then seen[i+1][v]=true end
 end
 if not snapped and m.output:get_value('disk_activity2')~=0 then m.video:snapshot();snapped=true end
 if m.time:as_double()>40 then
  assert(seen[2][1], 'FD2 read not indicated')
  assert(seen[3][1] and seen[4][1], 'HD/SHD reads not indicated')
  assert(seen[3][2] or seen[4][2], 'HD write not indicated')
  assert(next(seen[1])==nil, 'Empty FD1 incorrectly active')
  for i=0,3 do assert(m.output:get_value('disk_activity'..i)==0, 'Activity did not expire') end
  print('PASS: real boot FD2/HD/SHD read, HD write, FD1 isolation and lamp expiry')
  m:exit()
 end
end)
