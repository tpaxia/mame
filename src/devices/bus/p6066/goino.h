// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#ifndef MAME_MACHINE_P6066_GOINO_H
#define MAME_MACHINE_P6066_GOINO_H
#pragma once
#include "goino_state.h"
#include "keyboard.h"
#include "p6066.h"
#include "screen.h"
#include "sound/beep.h"

class p6066_discard_printer_device : public device_t
{
public:
	p6066_discard_printer_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);
	void tick(p6066_goino_state &state) { state.printer_tick(); }
	u8 status(const p6066_goino_state &state) const { return (state.printer_running || state.printer_feeding) ? 0x10 : 0; }
protected:
	virtual void device_start() override { }
};
DECLARE_DEVICE_TYPE(P6066_DISCARD_PRINTER, p6066_discard_printer_device)

class p6066_printer_slot_device : public device_t, public device_single_card_slot_interface<p6066_discard_printer_device>
{
public:
	template <typename T>
	p6066_printer_slot_device(const machine_config &mconfig, const char *tag, device_t *owner, T &&options, const char *default_card)
		: p6066_printer_slot_device(mconfig, tag, owner, 0) { set_options(std::forward<T>(options), default_card, false); }
	p6066_printer_slot_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);
protected:
	virtual void device_start() override { }
};
DECLARE_DEVICE_TYPE(P6066_PRINTER_SLOT, p6066_printer_slot_device)

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
		if (level == 2 && m_state.owned2)
		{
			if ((mask & 0x7f) != 0x7f) fatalerror("GOINO: unspecified printer column");
			data_w(level, data, mask);
			return;
		}
		if ((mask & 0xff00) != 0xff00)
			fatalerror("GOINO: unspecified ECD control lanes %04X", mask);
		data_w(level, data, mask);
	}
	virtual void command_word(unsigned level, u16 data, u16 mask) override
	{
		// GOINO printed pp.5,13: CAE supplies the same ECD command/data
		// decoder and ECOT strobes. ECOC does not select a second register.
		output_data_masked(level, data, mask);
	}
	u16 name_type_r(offs_t level);
	u8 input_data_r(offs_t level);
	void select_w(u8 data);
	void data_w(offs_t level, u16 data, u16 mask = 0xffff);
	virtual u8 irq_requests() const override { return m_state.irq_requests(); }
	virtual void interrupt_sync(u8 mask) override { m_state.synchronize(mask); }
	virtual void irq_ack(unsigned source) override;
	virtual void strobe(unsigned level) override { if (level == 2 && m_state.owned2) m_state.column_request = false; }
	virtual void irq_end(unsigned level) override { m_state.end(level); }
	// Peripheral-side signal boundary: logical TAS1..9, PRCAA and ERSIN.
	void keyboard_w(u16 code, bool ready) { m_state.keyboard_code = code & 0x1ff; m_state.keyboard_request = ready; }
	void keyboard_error_w(int asserted) { m_state.double_key_request = asserted; }
	DECLARE_INPUT_CHANGED_MEMBER(buttons_changed);
	DECLARE_INPUT_CHANGED_MEMBER(decimal_changed);
	auto auxiliary_input_cb() { return m_auxiliary_input_cb.bind(); }

	u32 screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect);
protected:
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual ioport_constructor device_input_ports() const override;
private:
	void update_outputs();
	TIMER_CALLBACK_MEMBER(timer_tick);
	TIMER_CALLBACK_MEMBER(beep_off);
	emu_timer *m_timer = nullptr;
	emu_timer *m_beep_timer = nullptr;
	required_ioport m_buttons;
	required_ioport m_buzzer_config;
	required_device<p6066_keyboard_device> m_keyboard;
	required_device<p6066_printer_slot_device> m_printer_slot;
	required_device<beep_device> m_beeper;
	bool m_keyboard_down = false, m_mode_down = false;
	u8 m_decimal_position = 15;
	void keyboard_command(unsigned level, u16 data);
	devcb_read8 m_auxiliary_input_cb;
	p6066_goino_state m_state;
	output_finder<16> m_lamps;
	output_finder<> m_selected, m_strobes, m_commands_seen, m_interrupts_blocked;
	output_finder<> m_lamp_word, m_display_strobes, m_display_ready, m_keyboard_mode;
	output_finder<> m_decimal_display;
};
DECLARE_DEVICE_TYPE(P6066_GOINO, p6066_goino_device)
#endif
