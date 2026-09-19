// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#include "emu.h"
#include "goino.h"

DEFINE_DEVICE_TYPE(P6066_GOINO, p6066_goino_device, "p6066_goino", "Olivetti P6066 GOINO/CONDY (partial)")

p6066_goino_device::p6066_goino_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, P6066_GOINO, tag, owner, clock)
	, device_p6066_card_interface(mconfig, *this)
	, m_buttons(*this, "BUTTONS")
	, m_keyboard(*this, "keyboard")
	, m_auxiliary_input_cb(*this, 0)
	, m_lamps(*this, "console_lamp%u", 0U)
	, m_selected(*this, "console_selected")
	, m_strobes(*this, "console_strobes")
	, m_commands_seen(*this, "commands_seen")
	, m_interrupts_blocked(*this, "interrupts_blocked")
	, m_lamp_word(*this, "console_lamps")
	, m_display_strobes(*this, "display_strobes")
	, m_display_ready(*this, "display_ready")
{
}

static INPUT_PORTS_START(goino)
	PORT_START("BUTTONS")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Down arrow (TASB)") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(p6066_goino_device::buttons_changed), 0)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Calculator mode") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(p6066_goino_device::buttons_changed), 0)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Break") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(p6066_goino_device::buttons_changed), 0)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Continue") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(p6066_goino_device::buttons_changed), 0)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Trace") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(p6066_goino_device::buttons_changed), 0)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Step") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(p6066_goino_device::buttons_changed), 0)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Print all") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(p6066_goino_device::buttons_changed), 0)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("No print") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(p6066_goino_device::buttons_changed), 0)
INPUT_PORTS_END
ioport_constructor p6066_goino_device::device_input_ports() const { return INPUT_PORTS_NAME(goino); }
INPUT_CHANGED_MEMBER(p6066_goino_device::buttons_changed) { m_state.buttons_w(m_buttons->read() | (m_keyboard_down ? 1 : 0)); }
void p6066_goino_device::device_add_mconfig(machine_config &config)
{
 P6066_KEYBOARD(config,m_keyboard);
 m_keyboard->data_cb().set([this](u16 data) { m_state.keyboard_code=data; });
 m_keyboard->ready_cb().set([this](int state) { m_state.keyboard_request=bool(state); });
 m_keyboard->error_cb().set([this](int state) { m_state.double_key_request=bool(state); });
 m_keyboard->down_cb().set([this](int state) { m_keyboard_down=bool(state); m_state.buttons_w(m_buttons->read() | (state?1:0)); });
 m_keyboard->mode_cb().set([this](int state) { if(state && !m_mode_down) m_state.basic_mode=!m_state.basic_mode; m_mode_down=bool(state); });
}
void p6066_goino_device::keyboard_command(unsigned level, u16 data)
{
 if (!m_state.enabled(level)) return;
 switch ((data>>8)&15) {
 case 7: m_keyboard->acknowledge(); break; // UTCAN
 case 9: m_keyboard->reset_error(); break; // RESIN
 }
}
TIMER_CALLBACK_MEMBER(p6066_goino_device::timer_tick) { m_state.timer_tick(); }
void p6066_goino_device::irq_ack(unsigned source)
{
	if (!m_state.acknowledge(source)) fatalerror("GOINO interrupt acknowledgement without request");
}

void p6066_goino_device::device_start()
{
	m_timer = timer_alloc(FUNC(p6066_goino_device::timer_tick), this);
	save_item(NAME(m_keyboard_down));
	save_item(NAME(m_mode_down));
	save_item(NAME(m_state.selected));
	save_item(NAME(m_state.matrix_request));
	save_item(NAME(m_state.column_request));
	save_item(NAME(m_state.button_request));
	save_item(NAME(m_state.pippo_request));
	save_item(NAME(m_state.keyboard_request));
	save_item(NAME(m_state.timer_request));
	save_item(NAME(m_state.double_key_request));
	save_item(NAME(m_state.pippo_enabled));
	save_item(NAME(m_state.timer_enabled));
	save_item(NAME(m_state.interrupts_blocked));
	save_item(NAME(m_state.commands_seen));
	save_item(NAME(m_state.lamp_shift));
	save_item(NAME(m_state.lamps));
	save_item(NAME(m_state.lamp_bits));
	save_item(NAME(m_state.lamp_strobes));
	save_item(NAME(m_state.display));
	save_item(NAME(m_state.display_position));
	save_item(NAME(m_state.display_ready));
	save_item(NAME(m_state.display_strobes));
	save_item(NAME(m_state.synchronized3));
	save_item(NAME(m_state.synchronized2));
	save_item(NAME(m_state.owned2));
	save_item(NAME(m_state.owned3));
	save_item(NAME(m_state.basic_mode));
	save_item(NAME(m_state.keyboard_code));
	save_item(NAME(m_state.buttons));
	save_item(NAME(m_state.input_select));
	save_item(NAME(m_state.printer_column));
	save_item(NAME(m_state.lamp_known_shift));
	save_item(NAME(m_state.lamps_known));
	save_item(NAME(m_state.display_known));

	machine().save().register_postload(save_prepost_delegate(FUNC(p6066_goino_device::update_outputs), this));
}

void p6066_goino_device::device_reset()
{
	// Deterministic development reset; physical latch reset coverage unverified.
	m_state = p6066_goino_state{};
	m_keyboard_down = m_mode_down = false;
	// Nominal timer period specified in GOINO printed pp.11,15. The
	// oscillator is free-running; TIMEN/FTIMN gate events, not its phase.
	m_timer->adjust(attotime::from_usec(6300), 0, attotime::from_usec(6300));
	update_outputs();
}

void p6066_goino_device::update_outputs()
{
	for (unsigned i = 0; i != 16; ++i) m_lamps[i] = BIT(m_state.lamps, i);
	m_selected = m_state.selected;
	m_strobes = m_state.lamp_strobes;
	m_commands_seen = m_state.commands_seen;
	m_interrupts_blocked = m_state.interrupts_blocked;
	m_lamp_word = m_state.lamps;
	m_display_strobes = m_state.display_strobes;
	m_display_ready = m_state.display_ready;
}


u16 p6066_goino_device::name_type_r(offs_t level)
{
	// Name lines float (logical 00); type comes from synchronized sources.
	return m_state.enabled(level) ? m_state.type() << 8 : 0;
}

u8 p6066_goino_device::input_data_r(offs_t level)
{
	if (!m_state.enabled(level)) return 0;
	switch (m_state.input_select)
	{
	case 0: return m_state.button_code();
	case 2: return m_state.key_data();
	case 1:
		// Explicit user-approved printer/decimal-status stub. This zero byte is
		// an inert emulation policy, NOT recovered DISL006 wiring or proof of
		// printer presence/readiness. Printer mechanics remain out of scope.
		if (m_auxiliary_input_cb.isunset()) return 0;
		return m_auxiliary_input_cb(1);
	default:
		// Fig.1.2: selector 3 is the specialization PROM, not covered by
		// the printer-status stub. It still requires a verified dump.
		if (m_auxiliary_input_cb.isunset())
			fatalerror("GOINO input %u requires specialization PROM", m_state.input_select);
		return m_auxiliary_input_cb(m_state.input_select);
	}
}

void p6066_goino_device::select_w(u8 data)
{
	m_state.select(data);
	update_outputs();
}

void p6066_goino_device::data_w(offs_t level, u16 data, u16 mask)
{
	const u32 display_before = m_state.display_strobes;
	if (!m_state.data(data, level, mask))
		fatalerror("GOINO bring-up: unsupported output %04X at level %u (%s)\n", data, unsigned(level), machine().describe_context());
	if (m_state.display_strobes != display_before)
		logerror("GOINO display strobe=%u column=%u data=%02X known=%02X ECD=%04X level=%u (%s)\n",
			m_state.display_strobes, (m_state.display_position + 223) % 224,
			data & 0xff, mask & 0xff, data, unsigned(level), machine().describe_context());
	keyboard_command(level, data);
	update_outputs();
}

u32 p6066_goino_device::screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	bitmap.fill(rgb_t(12, 16, 16), cliprect);
	if (!m_state.display_ready) return 0;
	// Functional dots, not multiplex timing. Vertical bit orientation remains provisional.
	const bool blink_on = (machine().time().as_ticks(8) & 1) == 0;
	for (unsigned x = 0; x != 222; ++x)
	{
		const u8 column = m_state.display[x + 2];
		if (m_state.display_known[x + 2] != 0xff) continue;
		if (BIT(column, 7) && !blink_on) continue;
		for (unsigned y = 0; y != 7; ++y)
			if (BIT(column, y))
				for (unsigned dy = 0; dy != 3; ++dy)
					for (unsigned dx = 0; dx != 3; ++dx)
						if (cliprect.contains(x * 4 + dx, y * 4 + dy))
							bitmap.pix(y * 4 + dy, x * 4 + dx) = rgb_t(255, 142, 48);
	}
	return 0;
}
