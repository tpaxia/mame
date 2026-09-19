// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#ifndef MAME_BUS_P6066_KEYBOARD_H
#define MAME_BUS_P6066_KEYBOARD_H
#pragma once
#include "keyboard_codes.h"

// Functional TM601 peripheral boundary. Host keys are already debounced;
// capacitive thresholds and encoder-internal debounce are not simulated.
class p6066_keyboard_device : public device_t
{
public:
 p6066_keyboard_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);
 auto data_cb() { return m_data_cb.bind(); }
 auto ready_cb() { return m_ready_cb.bind(); }
 auto error_cb() { return m_error_cb.bind(); }
 auto down_cb() { return m_down_cb.bind(); }
 auto mode_cb() { return m_mode_cb.bind(); }
 void acknowledge() { m_ready = false; m_ready_cb(0); }
 void reset_error() { m_error_cb(0); }
protected:
 virtual void device_start() override ATTR_COLD;
 virtual void device_reset() override ATTR_COLD;
 virtual ioport_constructor device_input_ports() const override;
private:
 TIMER_CALLBACK_MEMBER(scan);
 void emit(unsigned key, unsigned modifiers);
 required_ioport_array<3> m_keys;
 required_ioport m_modifiers;
 devcb_write16 m_data_cb;
 devcb_write_line m_ready_cb, m_error_cb, m_down_cb, m_mode_cb;
 emu_timer *m_scan = nullptr;
 std::array<u32,3> m_previous{};
 bool m_down_chord = false;
 int m_repeat_key = -1;
 unsigned m_repeat_ticks = 0;
 bool m_ready = false;
};
DECLARE_DEVICE_TYPE(P6066_KEYBOARD, p6066_keyboard_device)
#endif
