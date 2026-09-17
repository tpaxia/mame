-- license:BSD-3-Clause
-- copyright-holders: Salvatore Paxia
-- Observer for the reference CAROM and disk-121 boot-directory scan.
-- CAROM rewinds M4 after each sector: inspect RAM before that buffer is reused.
local m=manager.machine
local c=m.devices[":maincpu"]
local f=assert(m.devices[":bus:floppy:flodi"])
local s=c.spaces["program"]
local directory=assert(os.getenv("P6066_FLODI_RESULTS"))
local case=assert(os.getenv("P6066_FLODI_CASE"))
local status=assert(tonumber(os.getenv("P6066_FLODI_STATUS")))
local file=assert(io.open(directory.."/expected.bin","rb"))
local expected=file:read("a");file:close()
assert(#expected==22*128)
local verified=0
local payload={}
local done=false
local failure=nil
local function check(ok,message) if not ok and not failure then failure=message end end

-- Keep taps alive. Callbacks return no replacement data and never write RAM.
flodi_data_tap=s:install_read_tap(0x8173,0x8173,"flodi-payload-oracle",function()
    if done or failure or c.state["PHASE"].value~=0 or c.state["LEVEL"].value~=1 then return end
    local bytes=f:output("bytes_read"):get()
    local n=bytes//128
    check(bytes%128==0 and n==verified+1 and n<=22,"Missing/duplicate sector service")
    if failure then return end
    local got={}
    for a=0x40,0x7f do local w=s:read_u16(a);got[#got+1]=string.char(w>>8,w&255) end
    local actual=table.concat(got)
    check(actual==expected:sub((n-1)*128+1,n*128),"RAM payload differs at sector "..(n+4))
    payload[n]=actual
    verified=n
end)
flodi_end_tap=s:install_read_tap(0x82a4,0x82a4,"flodi-command-end-oracle",function()
    if done or c.state["PHASE"].value~=0 or f:output("sectors_read"):get()~=22 then return end
    done=true
    check(verified==22 and f:output("bytes_read"):get()==2816,"Incomplete transfer")
    check(c.state["LEVEL"].value==3,"End event did not reach level 3")
    check(c.state["L12"].value>>8==status,"Wrong completion/CRC/deleted status")
    check(c.state["INVALID"].value==(m.devices[":bus:microcode:me006"] and 16 or 30),"New invalid memory cycle during read")
    check(c.state["L4"].value==0x80,"CAROM did not rewind its sector buffer")
end)
emu.register_frame_done(function()
    if not done and not failure then return end
    if failure then print("FAIL: "..failure);m:exit();return end
    file=assert(io.open(directory.."/"..case.."-payload.bin","wb"))
    file:write(table.concat(payload));file:close()
    local panel=m.devices[":bus:console:goino"]
    local lamps=0
    for bit=0,15 do lamps=lamps|(panel:output("console_lamp"..bit):get()<<bit) end
    m.video:snapshot()
    print(string.format("PASS: FLODI %s; 22 sectors / 2816 bytes verified in RAM; end status %02X; lamps %04X",case,status,lamps))
    m:exit()
end)
