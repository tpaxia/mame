// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#ifndef MAME_BUS_OLIVETTI_L1_KEYBOARD_H
#define MAME_BUS_OLIVETTI_L1_KEYBOARD_H

#pragma once

#include "machine/keyboard.h"

class olivetti_l1_keyboard_device : public device_t, protected device_matrix_keyboard_interface<7U>
{
public:
	olivetti_l1_keyboard_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	auto data_handler() { return m_data_cb.bind(); }

	virtual ioport_constructor device_input_ports() const override ATTR_COLD;

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void key_make(uint8_t row, uint8_t column) override;
	virtual void key_break(uint8_t row, uint8_t column) override;

private:
	uint8_t scancode(uint8_t row, uint8_t column) const;

	devcb_write8 m_data_cb;
};

DECLARE_DEVICE_TYPE(OLIVETTI_L1_KEYBOARD, olivetti_l1_keyboard_device)

#endif // MAME_BUS_OLIVETTI_L1_KEYBOARD_H
