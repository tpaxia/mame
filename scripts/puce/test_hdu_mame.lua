-- license:BSD-3-Clause
-- copyright-holders: Salvatore Paxia
-- Synthetic PUCE fixture, not an ESE boot or a patch to original software.
-- Mount a disposable blank .phd on hard1; omit console and floppy boards.
local m=manager.machine
local c=m.devices[":maincpu"]
local ram=c.spaces["program"]
assert(m.devices[":bus:dma:rodma"] and m.devices[":bus:hdu:difo"])
assert(not m.devices[":bus:console:goino"] and not m.devices[":bus:floppy:flodi"])
local pc=0x9000
local function emit(v) ram:write_u16(pc,v);pc=pc+1 end
local function reg(n,v) emit(0x5000|(n<<8)|(v>>8));emit(0x7000|(n<<8)|(v&255)) end
local function command(v) reg(10,v);emit(0xfca2) end
local function start(op,sector,pointer,count,key,scan)
    assert(c.state["LEVEL"].value==4,"Fixture must run at base level")
    ram:write_u16(0x0203,0)
    pc=0x9000
    reg(1,0x9800);reg(5,0x0200)
    reg(15,1);emit(0xb1f4) -- ESE byte1 = selection50
    command(scan or 0);command(0x200);command(0x400|sector)
    command(0x600|((count-1)&255));command(0x800|(pointer&255));command(0xa00|(pointer>>8))
    command(0xc00|(key or 0));command(0xe00|op)
    -- Private-memory loop keeps ALFA synchronization running during DMA.
    emit(pc&0x1fff)
    c.state["PC"].value=0x9000
end
-- Shared memory selector and format templates, exact 20 bytes per slot.
ram:write_u16(0,0x0050)
for slot=0,48 do
    local t={255,255,255,255,255,0x55,0,slot,0x12,0x34,255,255,255,255,255,255,255,0x55,255,255}
    if slot==48 then for i=1,20 do t[i]=255 end end
    for i=0,9 do ram:write_u16(0x0800+slot*10+i,(t[i*2+1]<<8)|t[i*2+2]) end
end
-- Production 3A response: status+ECOT, NUC+ECOT, SAT; COM0 closes ECM3.
pc=0x9800
emit(0xf428);emit(0xee52);emit(0xf438);emit(0xee53);emit(0xb848);emit(0xee54)
reg(2,0xa55a);emit(0xee52);emit(0xbd00);emit(0x1800)
-- AUTOboot can arrive before reset's level-3 context has returned.
if c.state["LEVEL"].value==3 then
    ram:write_u16(0x9900,0xbd00);ram:write_u16(0x9000,0x1000)
    c.state["L0"].value=0x9000;c.state["PC"].value=0x9900
else
    ram:write_u16(0x9000,0x1000);c.state["PC"].value=0x9000
end
local phase=0
local frames=0
local function byte(a) local v=ram:read_u16(a>>1);return (v>>(a%2==0 and 8 or 0))&255 end
emu.register_frame_done(function()
    frames=frames+1
    assert(frames<180,"HDU integration timed out, phase "..phase)
    assert(c.state["STOPPED"].value==0,"Unexpected CPU stop")
    if phase==0 and c.state["LEVEL"].value==4 then
        start(0x80,0,0x1000,0);phase=1
    elseif ram:read_u16(0x0203)==0xa55a and c.state["LEVEL"].value==4 then
        local status=ram:read_u16(0x0200)&255
        assert(status==(phase==4 and 8 or 0),string.format("HDU phase %d status %02X",phase,status))
        if phase==1 then
            print("PASS: MAME PUCE->DIFO format, shared DMA, HDU image write and three-byte 3A response")
            for i=0,127 do ram:write_u16(0x0a00+i,(((i*2)&255)<<8)|((i*2+1)&255)) end
            start(0x10,0,0x1400,1);phase=2
        elseif phase==2 then
            for i=0,128 do ram:write_u16(0x0c00+i,0xcccc) end
            start(4,0,0x1801,1);phase=3
        elseif phase==3 then
            for i=0,255 do assert(byte(0x1801+i)==i,"DMA payload mismatch at "..i) end
            assert(byte(0x1800)==0xcc and byte(0x1901)==0xcc,"Odd DMA clobbered adjacent byte")
            print("PASS: MAME write/read round trip through actual DMA, odd destination and preserved surrounding bytes")
            start(0x20,0,0x1400,1,0,1);phase=4
        elseif phase==4 then
            assert((ram:read_u16(0x0201)&255)==0 and (ram:read_u16(0x0202)&255)==0,"Scan counters")
            start(8,0,0x10000,1);phase=5 -- invalid DMA address must be unused for VERIFY
        elseif phase==5 then
            print("PASS: MAME scan and DMA-free verify; actual completion IRQ releases for subsequent commands")
            m:exit();phase=6
        end
    end
end)
