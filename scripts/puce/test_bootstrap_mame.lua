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
    if checkpoint=="console-reset" then done=false;return end
    finish("PASS: bootstrap installed ME006; four disk blocks / 23424 bytes match; 206 sectors read; CAROM transfers to level 3 at word 1000")
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
