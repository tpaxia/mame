// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#include "emu.h"
#include "p6066_goino.h"

DEFINE_DEVICE_TYPE(P6066_GOINO, p6066_goino_device, "p6066_goino", "Olivetti P6066 GOINO/CONDY (partial)")

p6066_goino_device::p6066_goino_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, P6066_GOINO, tag, owner, clock)
	, m_lamps(*this, "console_lamp%u", 0U)
	, m_selected(*this, "console_selected")
	, m_strobes(*this, "console_strobes")
{
}

void p6066_goino_device::device_start()
{
	save_item(NAME(m_state.selected));
	save_item(NAME(m_state.lamp_shift));
	save_item(NAME(m_state.lamps));
	save_item(NAME(m_state.lamp_bits));
	save_item(NAME(m_state.lamp_strobes));
	save_item(NAME(m_state.display));
	save_item(NAME(m_state.display_position));
	save_item(NAME(m_state.display_ready));
	machine().save().register_postload(save_prepost_delegate(FUNC(p6066_goino_device::update_outputs), this));
}

void p6066_goino_device::device_reset()
{
	// Deterministic development reset; physical latch reset coverage unverified.
	m_state = p6066_goino_state{};
	update_outputs();
}

void p6066_goino_device::update_outputs()
{
	for (unsigned i = 0; i != 16; ++i) m_lamps[i] = BIT(m_state.lamps, i);
	m_selected = m_state.selected;
	m_strobes = m_state.lamp_strobes;
}


// GOINO direct selection is masked outside level 4 (manual PDF pp.8,16).
// No external controllers/interrupt owners exist in this development machine.
// Undriven inputs use logical zero; see docs/p6066/reset-inputs.md for evidence
// and the outstanding PUCE pull-up/CPU-name jumper verification.
u16 p6066_goino_device::name_type_r(offs_t level)
{
	if (m_state.selected && level == 4)
		fatalerror("GOINO bring-up: selected name/type input not implemented\n");
	return 0;
}

u8 p6066_goino_device::input_data_r(offs_t level)
{
	if (m_state.selected && level == 4)
		fatalerror("GOINO bring-up: selected data input not implemented\n");
	return 0;
}

void p6066_goino_device::select_w(u8 data)
{
	m_state.select(data);
	update_outputs();
}

void p6066_goino_device::data_w(offs_t level, u16 data)
{
	if (!m_state.data(data, level))
		fatalerror("GOINO bring-up: unsupported output %04X at level %u\n", data, unsigned(level));
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
