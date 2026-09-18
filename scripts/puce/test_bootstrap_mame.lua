-- license:BSD-3-Clause
-- copyright-holders: Salvatore Paxia
-- Read-only observation of the original loader. Exit at its natural firmware
-- entry checkpoint, before the separately unfinished console initialization.
local m=manager.machine
local c=m.devices[":maincpu"]
local s=c.spaces["program"]
local f=m.devices[":bus:floppy:flodi"]
local directory=assert(os.getenv("P6066_BOOT_RESULTS"))
local case=assert(os.getenv("P6066_BOOT_CASE"))
local done=false
local entry_checked=false
local checkpoint=os.getenv("P6066_BOOT_CHECKPOINT") or "firmware-entry"
local console=m.devices[":bus:console:goino"]
local read_shadow={}
local read_attempts=0
local saw_wait=false
local saw_cpu_service=false
bootstrap_software_writes=s:install_write_tap(0x6b80,0x6bbf,"software-read-data",function(a,d,mask)
    if checkpoint~="software-read" then return end
    if (mask&0xff00)~=0 then read_shadow[a*2]=(d>>8)&255 end
    if (mask&0xff)~=0 then read_shadow[a*2+1]=d&255 end
end)
local blocks={}
for line in io.lines(directory.."/blocks.txt") do
    local base,length=line:match("(%x+) (%d+)")
    blocks[#blocks+1]={tonumber(base,16),tonumber(length)}
end
local function finish(message)
    done=true
    print(message)
    m:exit() -- aborts the CPU timeslice; no PC, register or memory changes
end
bootstrap_entry_tap=s:install_read_tap(0x1000,0x1000,"bootstrap-entry-oracle",function()
    if done or entry_checked or c.state["PHASE"].value~=0 then return end
    if case~="installed" then finish("FAIL: absent ME006 still allowed firmware entry");return end
    if c.state["LEVEL"].value~=3 or c.state["INVALID"].value~=16 then finish("FAIL: wrong entry level or memory faults");return end
    done=true -- prevent debugger reads of word 1000 from re-entering this tap
    local total=0
    for _,block in ipairs(blocks) do
        local base,length=table.unpack(block)
        local file=assert(io.open(string.format("%s/expected-%04x.bin",directory,base),"rb"))
        local expected=file:read("a");file:close()
        local bytes={}
        for a=base,base+length//2-1 do local w=s:read_u16(a);bytes[#bytes+1]=string.char(w>>8,w&255) end
        local actual=table.concat(bytes)
        file=assert(io.open(string.format("%s/loaded-%04x.bin",directory,base),"wb"));file:write(actual);file:close()
        if actual~=expected then finish(string.format("FAIL: loaded block %04X differs",base));return end
        total=total+length
    end
    if total~=23424 or f:output("sectors_read"):get()~=206 then finish("FAIL: unexpected load length/read count");return end
    entry_checked=true
    if checkpoint~="firmware-entry" then done=false;return end
    finish("PASS: bootstrap installed ME006; four disk blocks / 23424 bytes match; 206 sectors read; CAROM transfers to level 3 at word 1000")
end)
bootstrap_software_tap=s:install_read_tap(0xa000,0xbfff,"software-read-oracle",function(a)
    if done or not entry_checked or checkpoint~="software-read" or c.state.PHASE.value~=0 then return end
    if a==0xa11a and c.state.L11.value==2 then saw_cpu_service=true end
    if a==0xa0f9 or a==0xb000 then finish("FAIL: unexpected error/context-switch path before first software read");return end
    if a~=0xb02b then return end
    local pc=c.state.L8.value*2
    if pc==0x2186 then read_attempts=read_attempts+1 end
    if pc==0x218e then saw_wait=true end
    if pc~=0x2192 then return end
    if not saw_cpu_service or not saw_wait or read_attempts<2 then
        finish("FAIL: missing CPU service, busy retry or WAIT");return
    end
    local file=assert(io.open(directory.."/expected-first-software-read.bin","rb"))
    local expected=file:read("a");file:close()
    local actual={}
    for i=0,127 do
        local b=read_shadow[0xd700+i]
        if not b then finish("FAIL: incomplete first software read");return end
        actual[#actual+1]=string.char(b)
    end
    if table.concat(actual)~=expected or f:output("sectors_read"):get()~=207 then
        finish("FAIL: first software sector differs from disk");return
    end
    finish(string.format("PASS: bootstrap software read; CPU name 02; %d SIO attempts while HOME busy; WAIT returned; cylinder 0 sector 5 / 128 bytes match at D700; 207 total sectors",read_attempts))
end)
-- Natural continuation immediately after the ninth startup command, SASPN.
bootstrap_console_tap=s:install_read_tap(0x1095,0x1095,"console-reset-oracle",function()
    if done or checkpoint~="console-reset" or c.state["PHASE"].value~=0 then return end
    if not entry_checked or c.state["LEVEL"].value~=4 then finish("FAIL: console checkpoint before validated loader entry");return end
    if console:output("commands_seen"):get()~=0x6bf1 or console:output("interrupts_blocked"):get()~=0 then
        finish("FAIL: incomplete console reset/release sequence");return
    end
    finish("PASS: bootstrap console reset; verified disk blocks; F4 F5 F6 F7 F8 F9 FB FD FE executed; SASPN releases inhibit; natural level-4 continuation at word 1095")
end)
emu.register_frame_done(function()
    if done then return end
    if c.state["STOPPED"].value~=0 then finish("FAIL: unsupported CPU operation before entry");return end
    if case=="removed" and c.state["INVALID"].value>30 then
        if c.state["INVALID"].value~=31 then finish("FAIL: unexpected missing-board faults");return end
        finish("PASS: bootstrap removed ME006; loader reaches an unclaimed A000 write; firmware entry is not reached")
    end
end)
bootstrap_stop_subscription=emu.add_machine_stop_notifier(function() if done then m.video:snapshot() end end)
