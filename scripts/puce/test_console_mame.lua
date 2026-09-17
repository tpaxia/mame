-- license:BSD-3-Clause
-- copyright-holders: Salvatore Paxia
-- Integration fixture: original CAROM, then synthetic PUCE in RAM, then reset.
-- See docs/p6066/console.md. No original ROM is modified.
local machine = manager.machine
local cpu = machine.devices[":maincpu"]
local console = machine.devices[":console"]
local screen = machine.screens[":screen"]
local function out(name) return console:output(name):get() end
local function check_cold()
    assert(cpu.state["STOPPED"].value == 1, "CPU did not stop")
    assert(cpu.state["IR"].value == 0x9632, "Expected ADDA")
    assert(cpu.state["PC"].value == 0x803a, "Unexpected next PC")
    assert(cpu.state["ECORN"].value == 0, "COM3 did not hold reset low")
    assert(cpu.state["L9"].value == 0 and cpu.state["L10"].value == 0xfd00, "Idle input bus checks")
    assert(out("console_strobes") == 256, "Expected 256 serial lamp strobes")
    for bit = 0, 15 do assert(out("console_lamp" .. bit) == 1, "Lamp bit missing") end
end
check_cold()
machine.video:snapshot()
print("PASS: cold CAROM selected GOINO, sent 256 lamp bits, executed COM3 and idle bus tests, stopped at ADDA/8039")

local frames = 0
local phase = 0
emu.register_frame_done(function()
    frames = frames + 1
    if phase == 0 and frames == 2 then
        local space = cpu.spaces["program"]
        -- Byte 0 is FF, byte 1 is 00: ESE must read the odd byte to select GOINO.
        space:write_u16(0, 0xff00)
        space:write_u16(0x1000, 0xbd00) -- exit level 3 to L0
        local pc = 0x2000
        local function emit(op) space:write_u16(pc, op); pc = pc + 1 end
        emit(0xbd30); emit(0xbd00) -- level-4 COM0 must not release ECORN
        emit(0x7f01); emit(0xb1f4) -- CRTA A15,1; ESE A15
        emit(0x5a40)
        for bit = 15, 0, -1 do
            emit(0x7a00 | ((0xa55a >> bit) & 1)); emit(0xfca0)
        end
        emit(0x5a20)
        -- Two prep bytes, then a deterministic diagonal pattern on 222 columns.
        for i = 0, 223 do
            emit(0x7a00 | (i < 2 and 0x7f or (1 << ((i - 2) % 7))))
            emit(0xfca0)
        end
        emit(0xf000) -- explicit implementation stop after this synthetic fixture
        cpu.state["L0"].value = 0x2000
        cpu.state["PC"].value = 0x1000 -- debugger import resumes the held CPU
        phase = 1
    elseif phase == 1 and frames >= 5 then
        assert(cpu.state["STOPPED"].value == 1 and cpu.state["IR"].value == 0xf000)
        assert(cpu.state["ECORN"].value == 0, "Level-4 COM0 unexpectedly released reset")
        assert(out("console_selected") == 1 and out("console_strobes") == 272)
        for bit = 0, 15 do
            assert(out("console_lamp" .. bit) == ((0xa55a >> bit) & 1), "Serial lamp mapping")
        end
        assert((screen:pixel(0, 0) & 0xffffff) == 0xff8e30, "First dot not rendered")
        assert((screen:pixel(0, 4) & 0xffffff) == 0x0c1010, "Preparation byte leaked")
        assert((screen:pixel(4, 4) & 0xffffff) == 0xff8e30, "Second column not rendered")
        machine.options.entries["snapname"]:value("console-fixture")
        machine.video:snapshot()
        machine.options.entries["snapname"]:value("console-cold")
        print("PASS: actual PUCE ESE odd-byte selection, DAE serial lamp pattern and 224-byte display rendering")
        machine.ioport.ports[":PANEL"].fields["Restart machine"]:set_value(1)
        phase = 2
    elseif phase == 2 then
        machine.ioport.ports[":PANEL"].fields["Restart machine"]:set_value(0)
        phase = 3
    elseif phase == 3 and frames >= 45 then
        check_cold()
        print("PASS: panel restart input resets CPU/console and repeats cold CAROM")
        phase = 4
    end
end)
