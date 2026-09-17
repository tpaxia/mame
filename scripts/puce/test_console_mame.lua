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
    assert(cpu.state["STOPPED"].value == 0, "Unexpected unsupported CPU operation")
    assert(cpu.state["IR"].value == 0x00b6 and (cpu.state["PC"].value == 0x80b6 or cpu.state["PC"].value == 0x80b7), "Expected firmware timeout loop")
    assert(cpu.state["ECORN"].value == 0, "Timeout COM3 did not assert controller reset")
    assert(cpu.state["LEVEL"].value == 4 and cpu.state["L1"].value == 0x8226, "Bootstrap handler not installed")
    assert(cpu.state["INVALID"].value == 30, "Unexpected memory enumeration faults")
    assert(machine.devices[":"]:output("floppy_selects"):get() == 1, "Bootstrap did not select controller E0")
    local space = cpu.spaces["program"]
    assert(space:read_u16(2) == 0x4000 and space:read_u16(3) == 0x1000, "Bootstrap parameters")
    assert((space:read_u16(7) >> 8) == 0x80, "RAM extent byte")
    assert(out("console_strobes") == 272, "Expected startup plus timeout lamps")
    for bit = 0, 15 do assert(out("console_lamp" .. bit) == ((0x8084 >> bit) & 1), "Timeout lamp pattern") end
end
check_cold()
machine.video:snapshot()
print("PASS: cold CAROM selected GOINO, sent 256 lamp bits, completed memory enumeration, selected floppy E0 and displayed firmware timeout pattern 8084")

local frames = 0
local phase = 0
emu.register_frame_done(function()
    frames = frames + 1
    assert(frames < 600 or phase == 4, "Integration fixture timed out")
    if phase == 0 and frames == 2 then
        local space = cpu.spaces["program"]
        -- Byte 0 is FF, byte 1 is 00: ESE must read the odd byte to select GOINO.
        space:write_u16(0, 0xff00)
        local pc = 0x2000
        local function emit(op) space:write_u16(pc, op); pc = pc + 1 end
        space:write_u16(0x180, 0x1234)
        space:write_u16(0x40, 0x1234)
        emit(0x5203); emit(0x7200); emit(0x53a5); emit(0x8923) -- BMI M2,B3, even
        emit(0x7201); emit(0x535a); emit(0x8923) -- odd, preserve high lane
        emit(0x5cab); emit(0x7c80); emit(0x89c3) -- short address ignores B12
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
        cpu.state["PC"].value = 0x2000 -- already level 4; debugger import resumes the held CPU
        phase = 1
    elseif phase == 1 and frames >= 5 then
        assert(cpu.state["STOPPED"].value == 1 and cpu.state["IR"].value == 0xf000)
        assert(cpu.spaces["program"]:read_u16(0x180) == 0xa55a, "BMI even/odd lane ordering")
        assert(cpu.spaces["program"]:read_u16(0x40) == 0x5a34 and cpu.state["L12"].value == 0xab80, "BMI short address")
        assert(cpu.state["ECORN"].value == 0, "Level-4 COM0 unexpectedly released reset")
        assert(out("console_selected") == 1 and out("console_strobes") == 288)
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
    elseif phase == 3 and cpu.state["IR"].value == 0x00b6 and out("console_strobes") == 272 then
        check_cold()
        print("PASS: panel restart input resets CPU/console and repeats cold CAROM")
        phase = 4
        machine.video:snapshot()
        machine:exit()
    end
end)
