// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#ifndef MAME_MACHINE_P6066_GOINO_H
#define MAME_MACHINE_P6066_GOINO_H
#pragma once
#include "p6066_goino_state.h"
#include "screen.h"

class p6066_goino_device : public device_t
{
public:
	p6066_goino_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);
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
	output_finder<> m_selected, m_strobes;
};
DECLARE_DEVICE_TYPE(P6066_GOINO, p6066_goino_device)
#endif
