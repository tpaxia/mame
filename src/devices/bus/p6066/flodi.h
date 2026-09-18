// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#ifndef MAME_BUS_P6066_FLODI_H
#define MAME_BUS_P6066_FLODI_H
#pragma once
#include "p6066.h"
#include "flodi_latches.h"
#include "flodi_irq.h"
#include "imagedev/floppy.h"
class p6066_flodi_device : public device_t, public device_p6066_card_interface
{
public:
	p6066_flodi_device(const machine_config &,const char *,device_t *,u32 clock=0);
	virtual void select(u8 name) override;
	virtual u8 irq_requests() const override { return m_irq.requests(); }
	virtual void interrupt_sync(u8 mask) override;
	virtual void irq_ack(unsigned source) override;
	virtual void irq_end(unsigned level) override;
	virtual u16 name_type(unsigned level) override;
	virtual u8 input_data(unsigned level) override;
	virtual void command(unsigned level,u8 data) override;
	virtual void strobe(unsigned level) override;
	virtual void output_data(unsigned level,u16 data) override;
	virtual void output_data_masked(unsigned level,u16 data,u16 mask) override
	{
		// FLODI data and command inputs use ECD0..7 (functional diagram).
		if ((mask & 0x00ff) != 0x00ff) fatalerror("FLODI: unspecified low ECD data requires electrical bus model");
		output_data(level,data);
	}
	virtual void command_word(unsigned level,u16 data,u16 mask) override
	{
		if ((mask & 0x00ff) != 0x00ff) fatalerror("FLODI: unspecified low ECD command requires electrical bus model");
		command(level,data);
	}
	virtual void control(unsigned level,u8 signal) override;
	virtual void controller_reset(bool asserted) override;
protected:
	virtual void device_start() override;
	virtual void device_reset() override;
	virtual void device_add_mconfig(machine_config &config) override;
private:
	floppy_image_device *drive() const;
	TIMER_CALLBACK_MEMBER(mechanical_tick);
	emu_timer *m_timer=nullptr, *m_byte_timer=nullptr;
	TIMER_CALLBACK_MEMBER(byte_tick);
	void load_track();
	void next_id();
	void request3(u8 type,u8 status=0);
	void stop_read();
	void schedule_byte(attotime when);
	void index_changed(floppy_image_device *floppy, int state);
	std::array<u8,100000> m_bits{};
	unsigned m_bit_count=0, m_cursor=0, m_id_pos=0, m_data_pos=0;
	u8 m_header[5]{}, m_payload[1024]{};
	unsigned m_length=0, m_byte=0, m_id_byte=0;
	bool m_reading=false, m_id_phase=false, m_data_irq=false, m_mismatch=false;
	bool m_crc_gate=false, m_last_sector=false, m_id_crc=false, m_data_crc=false;
	u8 m_data_mark=0, m_expected_mark=0xfb, m_expected_clock=0xc7, m_end_status=0;
	attotime m_id_time, m_next_index, m_track_epoch, m_byte_time;
	int m_finish_byte=-1, m_crc_start=-1;
	bool m_payload_start=false;
	u32 m_sectors_read=0, m_bytes_read=0;
	output_finder<> m_sector_output, m_byte_output;
	bool m_local[2]{}, m_motion=false, m_direction=false, m_settle=false;
	bool m_index=false;
	p6066_flodi_irq m_irq;
	p6066_flodi_latches m_latches;
	void latch_command(u8 previous);
	void start_transfer();
	required_device_array<floppy_connector,2> m_drives;
	u8 m_selected=0, m_active_type=0;
	bool m_reset=true;
};
DECLARE_DEVICE_TYPE(P6066_FLODI,p6066_flodi_device)
#endif
