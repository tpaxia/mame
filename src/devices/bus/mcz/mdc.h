// license: BSD-3-Clause
// copyright-holders: Salvatore Paxia
/***************************************************************************

    MCZ MDC Floppy Module

***************************************************************************/

#ifndef MAME_MCZ_MDC_H
#define MAME_MCZ_MDC_H

#pragma once

#include "sysbus.h"
#include "machine/z80pio.h"
#include "machine/fdc_pll.h"
#include "imagedev/floppy.h"
#include "formats/mcz_dsk.h"


class mcz_floppy_device : public device_t, public device_mcz_sysbus_card_interface
{
public:
	// construction/destruction
	mcz_floppy_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	//virtual uint16_t floppy_r(offs_t offset, uint16_t mem_mask = ~0) override;
	//virtual void floppy_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0) override;
	void floppy_index_cb(floppy_image_device *floppy, int state);
protected:
	// device_t implementation
	virtual void device_add_mconfig(machine_config &config) override;
	virtual void device_start() override;
	virtual void device_reset() override;

private:
	bool m_reading;     
	bool m_writing;     
	
	uint16_t m_crc;     // I-A62
	bool m_crc_enabled; // I-A58-13
	bool m_crc_out;     // C-A16-2
	uint8_t m_data_sr; // C-A36 & C-A29 (MSB), next byte in LSB
	bool m_last_data_bit;   // I-A48-6
	uint16_t m_clock_sr;    // C-A34 & C-A27 (MSB), next byte in LSB
	attotime m_last_f_time;
	bool m_clock_gate;  // I-A10-8
	bool m_amwrt;       // I-A58-6
	bool m_dlyd_amwrt;
	uint8_t m_sync_cnt; // U28 & U73
	bool m_lckup;
	bool m_accdata;
	bool m_overrun;
	bool m_timeout;
	
		// PLL
	fdc_pll_t m_pll;

// Timers
	emu_timer *m_timeout_timer;
	emu_timer *m_byte_timer;
	emu_timer *m_f_timer;
	emu_timer *m_half_bit_timer;

	TIMER_CALLBACK_MEMBER(timeout_tick);
	TIMER_CALLBACK_MEMBER(byte_tick);
	TIMER_CALLBACK_MEMBER(f_tick);
	TIMER_CALLBACK_MEMBER(half_bit_timer_tick);
	
	required_device_array<floppy_connector, 8> m_floppy;
	required_device<z80pio_device> m_pio;
	int8_t m_diskSelected;
	//uint8_t m_track;
	bool m_indexPulse;
	floppy_image_device *m_current_drive;

	uint8_t pio_read(offs_t o);
	void pio_write(offs_t o, uint8_t data);
	uint8_t data_read(offs_t o);
	void data_write(offs_t o, uint8_t data);
	void intrq_w(int state); 
	void drq_w(int state);

	bool shift_sr(uint8_t& sr , bool input_bit);
	void read_bit(bool crc_upd);
	void get_next_transition(const attotime& from_when , attotime& edge);
	attotime get_half_bit_cell_period(void) const;
	void set_rd_wr(bool new_rd , bool new_wr);
	uint8_t aligned_rd_data(uint16_t sr);
	void rd_bits(unsigned n);
	bool update_crc(bool bit);
};

// device type definition
DECLARE_DEVICE_TYPE(MCZ_FLOPPY, mcz_floppy_device)


#endif // MAME_MCZ_MDC_H
