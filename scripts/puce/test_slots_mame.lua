-- license:BSD-3-Clause
-- copyright-holders: Salvatore Paxia
-- Run at 7 seconds with FLODI removed and default RAM, ram3 empty,
-- ram3=ram8, or console empty. This keeps the no-controller CAROM oracle.
local m=manager.machine
local c=m.devices[":maincpu"]
assert(not m.devices[":bus:floppy:flodi"],"Removed controller still instantiated")
local ram8=m.devices[":bus:ram3:ram8"]
local ram16=m.devices[":bus:ram3:ram16"]
local extent=ram8 and 0x70 or (ram16 and 0x80 or 0x60)
local invalid=(ram8 and 34 or (ram16 and 30 or 38))-(m.devices[":bus:microcode:me006"] and 14 or 0)
assert(c.state["STOPPED"].value==0 and c.state["IR"].value==0x00b6,"CAROM did not reach controller timeout")
assert((c.spaces["program"]:read_u16(7)>>8)==extent,"Board population not reflected in RAM extent")
assert(c.state["INVALID"].value==invalid,"Missing boards did not produce invalid cycles")
assert(m.devices[":bus"]:output("floppy_selects"):get()==1,"Bootstrap not reached")
print(string.format("PASS: configurable boards, RAM extent %02X, %d unpopulated blocks, console %s",extent,invalid,m.devices[":bus:console:goino"] and "installed" or "removed"))
m:exit()
