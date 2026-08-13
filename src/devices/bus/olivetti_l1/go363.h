// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia

#ifndef MAME_BUS_OLIVETTI_L1_GO363_H
#define MAME_BUS_OLIVETTI_L1_GO363_H

#pragma once

#include "l1.h"

#include "machine/pit8253.h"
#include "machine/upd7261.h"

class olivetti_l1_go363_device : public device_t, public device_olivetti_l1_card_interface
{
public:
	olivetti_l1_go363_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock = 0);

	virtual u8 io_r(offs_t offset) override;
	virtual void io_w(offs_t offset, u8 data) override;
	virtual u16 viack_r() override;
	virtual olivetti_l1_bus_device::interrupt_level vi_level() const override { return olivetti_l1_bus_device::interrupt_level::l2; }

protected:
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	TIMER_CALLBACK_MEMBER(command_done);
	TIMER_CALLBACK_MEMBER(board_timer_done);
	void start_command();
	void start_transfer();
	void hdc_dreq_w(int state);
	void hdc_int_w(int state);
	void timer_w(unsigned channel, u8 data);
	void timer_control_w(u8 data);
	void update_vi();

	required_device<upd7261_device> m_hdc;
	optional_device_array<harddisk_image_device, 2> m_drive;
	required_device<pit8253_device> m_timer;
	emu_timer *m_command_timer = nullptr;
	emu_timer *m_board_timer = nullptr;

	u32 m_dma_address = 0;
	u16 m_transfer_head = 0;
	u16 m_transfer_count = 0;
	u16 m_transfer_cylinder = 0;
	u16 m_transfer_sector = 0;
	u16 m_transfer_latch = 0;
	u8 m_transfer_pair = 0;
	u16 m_command = 0;
	u16 m_parameter = 0;
	u8 m_start_low = 0;
	u8 m_status = 0;
	u16 m_result = 0;
	u16 m_diagnostic_control = 0;
	u16 m_diagnostic_data = 0;
	u8 m_diagnostic_fifo[8]{};
	u8 m_diagnostic_fifo_count = 0;
	u8 m_diagnostic_fifo_index = 0;
	bool m_diagnostic_fifo_read = false;
	u8 m_selected_unit = 0;
	u8 m_vector = 0;
	bool m_vector_loaded = false;
	bool m_interrupt = false;
	bool m_hdc_interrupt = false;
	bool m_timer_interrupt = false;
	bool m_timer_interrupt_enabled = false;
	bool m_diagnostic_interrupt = false;
	bool m_diagnostic_interrupt_enabled = false;
	bool m_diagnostic_vi = false;
	u16 m_timer_count[2]{};
	u8 m_timer_write_phase[2]{};
};

DECLARE_DEVICE_TYPE(OLIVETTI_L1_GO363, olivetti_l1_go363_device)

#endif // MAME_BUS_OLIVETTI_L1_GO363_H
