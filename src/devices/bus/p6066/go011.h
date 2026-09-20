// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#ifndef MAME_BUS_P6066_GO011_H
#define MAME_BUS_P6066_GO011_H
#pragma once
#include "p6066.h"
#include "go011_state.h"
#include "screen.h"

class p6066_go011_device : public device_t, public device_p6066_card_interface
{
public:
	p6066_go011_device(const machine_config &, const char *, device_t *, u32 clock = 0);
	virtual void select(u8 name) override { m_state.select(name); }
	virtual bool direct_selected() const override { return m_state.selected; }
	virtual void controller_reset(bool asserted) override { m_state.controller_reset(asserted); }
	virtual void command_word(unsigned level, u16 data, u16 mask) override;
	virtual void output_data_masked(unsigned level, u16 data, u16 mask) override;
	virtual u8 input_data(unsigned level) override;
	virtual u16 name_type(unsigned level) override;
	virtual void control(unsigned level, u8 signal) override;
	virtual void strobe(unsigned level) override;
protected:
	virtual void device_add_mconfig(machine_config &config) override;
	virtual void device_start() override;
	virtual void device_reset() override;
private:
	p6066_go011_state m_state;
	u32 screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect);
	void check(p6066_go011_state::result result, const char *operation, u16 data, u16 mask);
};
DECLARE_DEVICE_TYPE(P6066_GO011, p6066_go011_device)
#endif
