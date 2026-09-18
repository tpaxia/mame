// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#ifndef MAME_MACHINE_P6066_GOINO_H
#define MAME_MACHINE_P6066_GOINO_H
#pragma once
#include "goino_state.h"
#include "p6066.h"
#include "screen.h"

class p6066_goino_device : public device_t, public device_p6066_card_interface
{
public:
	p6066_goino_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);
	virtual void select(u8 name) override { select_w(name); }
	virtual bool direct_selected() const override { return m_state.selected; }
	virtual u16 name_type(unsigned level) override { return name_type_r(level); }
	virtual u8 input_data(unsigned level) override { return input_data_r(level); }
	virtual void output_data(unsigned level, u16 data) override { data_w(level,data); }
	virtual void output_data_masked(unsigned level, u16 data, u16 mask) override
	{
		// GOINO command decoder uses ECD8..11; CONDY's data strobes
		// additionally consume the low byte. Do not manufacture that byte.
		if ((mask & 0xff00) != 0xff00 || ((data & 0x6000) && (mask & 0xff) != 0xff))
			fatalerror("GOINO: unspecified ECD lanes %04X require electrical bus model", mask);
		data_w(level,data);
	}
	u16 name_type_r(offs_t level);
	u8 input_data_r(offs_t level);
	void select_w(u8 data);
	void data_w(offs_t level, u16 data);
	u32 screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect);
protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
private:
	void update_outputs();
	p6066_goino_state m_state;
	output_finder<16> m_lamps;
	output_finder<> m_selected, m_strobes, m_commands_seen, m_interrupts_blocked;
};
DECLARE_DEVICE_TYPE(P6066_GOINO, p6066_goino_device)
#endif
