// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#include "emu.h"
#include "go011.h"

DEFINE_DEVICE_TYPE(P6066_GO011, p6066_go011_device, "p6066_go011", "Olivetti GO011 video memory (experimental)")
p6066_go011_device::p6066_go011_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, P6066_GO011, tag, owner, clock), device_p6066_card_interface(mconfig, *this) { }

void p6066_go011_device::device_add_mconfig(machine_config &config)
{
	// Diagnostic RAM presentation, not recovered GO011 control/blanking.
	// Nominal refresh: DSM6660 graphics manual 3976460 A, appendix C.
	auto &screen(SCREEN(config, "framebuffer"));
	screen.set_refresh_hz(42.52);
	screen.set_size(p6066_go011_state::visible_width, p6066_go011_state::visible_height);
	screen.set_visarea_full();
	screen.set_screen_update(FUNC(p6066_go011_device::screen_update));
}

u32 p6066_go011_device::screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	for (int y = cliprect.min_y; y <= cliprect.max_y; ++y)
		for (int x = cliprect.min_x; x <= cliprect.max_x; ++x)
			bitmap.pix(y, x) = (!m_state.blanked && ((m_state.diagnostic_pixel(x, y) != m_state.inverted)
				|| m_state.text_cursor_pixel(x, y, ((screen.frame_number() / 21) & 1) == 0))) ? rgb_t::white() : rgb_t::black();
	return 0;
}

void p6066_go011_device::device_start()
{
	save_item(NAME(m_state.ram));
	save_item(NAME(m_state.written));
	save_item(NAME(m_state.address));
	save_item(NAME(m_state.address_known));
	save_item(NAME(m_state.selected));
	save_item(NAME(m_state.reset_held));
	save_item(NAME(m_state.inverted));
	save_item(NAME(m_state.blanked));
	save_item(NAME(m_state.access_mode));
	save_item(NAME(m_state.control_writes));
	save_item(NAME(m_state.pointer_x_counter));
	save_item(NAME(m_state.pointer_y_counter));
	save_item(NAME(m_state.pointer_x_known));
	save_item(NAME(m_state.pointer_y_known));
}
void p6066_go011_device::device_reset() { m_state.reset(); }
void p6066_go011_device::check(p6066_go011_state::result result, const char *operation, u16 data, u16 mask)
{
	if (result == p6066_go011_state::result::ok) return;
	const char *reason = "unsupported operation";
	switch (result)
	{
	case p6066_go011_state::result::lanes: reason = "unestablished ECD lane wiring"; break;
	case p6066_go011_state::result::address_range: reason = "unestablished address decode"; break;
	case p6066_go011_state::result::address_unknown: reason = "address requires CAE; advance/retention not established"; break;
	case p6066_go011_state::result::unwritten: reason = "uninitialised video RAM; power-on/reset value not established"; break;
	default: break;
	}
	fatalerror("GO011 evidence boundary: %s data=%04X mask=%04X address=%04X: %s (%s)\n",
		operation, data, mask, m_state.address, reason, machine().describe_context());
}
void p6066_go011_device::command_word(unsigned level, u16 data, u16 mask)
{
	if (level != 4 || !m_state.selected) return;
	check(m_state.command(data, mask), "CAE", data, mask);
}
void p6066_go011_device::output_data_masked(unsigned level, u16 data, u16 mask)
{
	if (level != 4 || !m_state.selected) return;
	check(m_state.output(data, mask), "DATA", data, mask);
}
u8 p6066_go011_device::input_data(unsigned level)
{
	if (level != 4 || !m_state.selected) return 0;
	u8 data = 0;
	check(m_state.input(data), "INPUT", 0, 0x00ff);
	return data;
}
u16 p6066_go011_device::name_type(unsigned level)
{
	if (level != 4 || !m_state.selected) return 0;
	// Functional model: implemented operations complete synchronously.
	// ETIB bit0 is busy; the name occupies the upper nibble of ENUA.
	return 0x00f0;
}
void p6066_go011_device::control(unsigned level, u8 signal)
{
	if (level == 4 && m_state.selected)
		check(p6066_go011_state::result::operation, "CONTROL", signal, 0x00ff);
}
void p6066_go011_device::strobe(unsigned level)
{
	if (level == 4 && m_state.selected)
		check(p6066_go011_state::result::operation, "standalone ECOT", 0, 0);
}
