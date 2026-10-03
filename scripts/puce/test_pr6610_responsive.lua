-- license:BSD-3-Clause
-- copyright-holders: Salvatore Paxia
-- Host layout only: measure the production callback bounds; never alter guest state.
local m = manager.machine
local target = m.render.targets[1]
local ports = m.ioport.ports[":OUTPUT_VIEW"].fields
local frame = 0
local window_width, height

local function check(view)
    assert(target.current_view.name == view, "wrong selected view")
    assert(target.width == window_width and target.height == height,
        "view selection must not resize the host window")
    local bounds = target.current_view.items["console_case"].bounds
    local w, h = bounds.width * target.width, bounds.height * target.height
    assert(math.abs(w / h - 112 / 44) < 0.01, "console aspect ratio changed")
    assert(math.abs(bounds.y1 - 1) < 0.0001, "blank space below console")
    assert(math.abs((bounds.x0 + bounds.x1) / 2 - 0.5) < 0.0001, "console not centered")
    print(string.format("%s: window %dx%d, console %.1fx%.1f", view, target.width, target.height, w, h))
end

emu.register_frame_done(function()
    frame = frame + 1
    if frame == 20 then
        window_width, height = target.width, target.height
        check("Video, Printer and Console")
        ports["Show video"]:set_value(1)
    elseif frame == 23 then
        ports["Show video"]:set_value(0)
    elseif frame == 40 then
        check("Video and Console")
        ports["Show printer"]:set_value(1)
    elseif frame == 43 then
        ports["Show printer"]:set_value(0)
    elseif frame == 60 then
        check("Printer and Console")
        ports["Show printer"]:set_value(1)
    elseif frame == 63 then
        ports["Show printer"]:set_value(0)
    elseif frame == 70 then
        check("Printer and Console")
        ports["Show video and printer"]:set_value(1)
    elseif frame == 73 then
        ports["Show video and printer"]:set_value(0)
    elseif frame == 90 then
        check("Video, Printer and Console")
        print("PASS: fixed host window and centered static console in all views")
        m:exit()
    end
end)
