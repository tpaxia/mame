// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia

#ifndef MAME_BUS_OLIVETTI_L1_GO252_H
#define MAME_BUS_OLIVETTI_L1_GO252_H

#pragma once

#include "l1.h"

#include "keyboard.h"
#include "video/mc6845.h"

#include "emupal.h"
#include "screen.h"

class olivetti_l1_go252_device : public device_t, public device_olivetti_l1_card_interface
{
public:
	olivetti_l1_go252_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock = 0);

	auto crtc_write_callback() { return m_crtc_write_cb.bind(); }

	bool keyboard_data_available() const { return m_kbd_count != 0; }
	u8 keyboard_data_r();
	void keyboard_data_w(u8 data) { m_kdc_data = data; }

	u8 vram_r(offs_t offset) const { return m_vram[offset & 0x0fff]; }
	void vram_w(offs_t offset, u8 data) { m_vram[offset & 0x0fff] = data; }

	virtual u8 io_r(offs_t offset) override;
	virtual void io_w(offs_t offset, u8 data) override;
	virtual u16 viack_r() override;
	virtual olivetti_l1_bus_device::interrupt_level vi_level() const override { return olivetti_l1_bus_device::interrupt_level::l1b; }
	virtual bool memory_claims(offs_t address) const override { return (address & 0xff0000) == 0xff0000; }
	virtual u8 memory_r(offs_t address) override { return vram_r(address); }
	virtual void memory_w(offs_t address, u8 data) override { vram_w(address, data); }

protected:
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	void kdc_queue(u8 data);
	void update_vi();
	void palette_init(palette_device &palette) ATTR_COLD;
	MC6845_UPDATE_ROW(crtc_update_row);

	required_device<mc6845_device> m_crtc;
	required_device<palette_device> m_palette;
	required_device<screen_device> m_screen;
	required_device<olivetti_l1_keyboard_device> m_keyboard;

	devcb_write8 m_crtc_write_cb;

	std::unique_ptr<u8[]> m_vram;
	u8 m_crtc_index = 0;
	u8 m_crtc_max_ras = 16;
	u8 m_kdc_ctrl = 0;
	u8 m_kdc_data = 0;
	u8 m_kdc_vector = 0x28;
	bool m_kdc_pending = false;
	bool m_kdc_data_armed = false;
	u8 m_kbd_fifo[16] = {};
	u8 m_kbd_head = 0;
	u8 m_kbd_tail = 0;
	u8 m_kbd_count = 0;
	u8 m_kbd_init_step = 0;
	u8 m_kbd_probe_step = 0;
	bool m_kbd_irq_mode = false;
};

DECLARE_DEVICE_TYPE(OLIVETTI_L1_GO252, olivetti_l1_go252_device)

#endif // MAME_BUS_OLIVETTI_L1_GO252_H
