// license:BSD-3-Clause
// copyright-holders:Salvatore Paxia

// CPU19 bring-up: register operations and basic byte memory transfers.
// Timing is provisional (one fetch + one execute scheduling quantum).
// ESE/DAE emit transaction callbacks; DMA waits and interrupt logic are absent.
// Stop explicitly on unsupported operations; never substitute successful I/O.

#include "emu.h"
#include "puce.h"
#include "pucedasm.h"

DEFINE_DEVICE_TYPE(PUCE, puce_device, "puce", "Olivetti PUCE (CPU19)")

puce_device::puce_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: cpu_device(mconfig, PUCE, tag, owner, clock)
	, m_program_config("program", ENDIANNESS_BIG, 16, 16, -1)
	, m_select_cb(*this)
	, m_data_cb(*this)
	, m_stopped_cb(*this)
{
}

device_memory_interface::space_config_vector puce_device::memory_space_config() const
{
	return {{ AS_PROGRAM, &m_program_config }};
}

void puce_device::device_start()
{
	space(AS_PROGRAM).specific(m_program);
	set_icountptr(m_icount);
	save_item(NAME(m_core.l));
	save_item(NAME(m_core.di));
	save_item(NAME(m_core.level));
	save_item(NAME(m_core.active));
	save_item(NAME(m_ir));
	save_item(NAME(m_fetch_pc));
	save_item(NAME(m_phase));
	save_item(NAME(m_stopped));
	state_add(STATE_GENPC, "PC", m_debug_pc).callimport().callexport();
	state_add(STATE_GENPCBASE, "CURPC", m_fetch_pc).noshow();
	state_add(STATE_GENFLAGS, "DI", m_core.di);
	state_add(1, "LEVEL", m_core.level).readonly();
	state_add(2, "IR", m_ir).readonly();
	state_add(4, "STOPPED", m_stopped).readonly();
	state_add(3, "PHASE", m_phase).readonly();
	for (unsigned i = 0; i != 16; ++i)
		state_add(16 + i, util::string_format("L%u", i).c_str(), m_core.l[i]);
}

void puce_device::device_reset()
{
	m_core.reset();
	m_stopped = false;
	m_stopped_cb(0);
	m_phase = 0;
	m_fetch_pc = m_core.pc();
	m_ir = 0;
}

void puce_device::state_import(const device_state_entry &entry)
{
	if (entry.index() == STATE_GENPC)
	{
		m_core.set_pc(m_debug_pc);
		m_stopped = false;
		m_stopped_cb(0);
		m_fetch_pc = m_core.pc();
		m_phase = 0;
	}
}

void puce_device::state_export(const device_state_entry &entry)
{
	if (entry.index() == STATE_GENPC) m_debug_pc = m_core.pc();
}

std::unique_ptr<util::disasm_interface> puce_device::create_disassembler()
{
	return std::make_unique<puce_disassembler>();
}

void puce_device::execute_run()
{
	while (m_icount > 0)
	{
		if (m_stopped) { m_icount = 0; return; }
		if (m_phase == 0)
		{
			m_fetch_pc = m_core.pc();
			debugger_instruction_hook(m_fetch_pc);
			m_ir = m_program.read_word(m_fetch_pc);
			m_core.advance();
			m_phase = 1;
		}
		else
		{
			bool done = m_core.execute_register(m_ir);
			const unsigned hi = m_ir >> 8, x = (m_ir >> 4) & 15, y = m_ir & 15;
			if (!done && (hi == 0xa8 || hi == 0x91))
			{
				const u16 address = m_core.indirect(x);
				const unsigned shift = puce_state::byte_shift(address);
				const u16 mask = puce_state::byte_mask(address);
				if (hi == 0xa8)
					m_program.write_word(address >> 1, u16(m_core.a(y)) << shift, mask);
				else
					m_core.set_a(y, m_program.read_word(address >> 1, mask) >> shift);
				done = true;
			}
			if (!done && (m_ir & 0xff0f) == 0xb104)
			{
				const u16 address = m_core.indirect(x);
				m_select_cb(m_program.read_word(address >> 1, puce_state::byte_mask(address)) >> puce_state::byte_shift(address));
				done = true;
			}
			if (!done && (m_ir & 0xff0f) == 0xfc00)
			{
				m_data_cb(m_core.level, m_core.l[x]);
				done = true;
			}
			if (!done)
			{
				const auto message = util::string_format("PUCE bring-up: unsupported %04X at word %04X (level %u, next PC %04X)\n",
					m_ir, m_fetch_pc, m_core.level, m_core.pc());
				if (!m_hold_on_unsupported) fatalerror("%s", message);
				osd_printf_info("%s", message);
				m_stopped = true;
				m_stopped_cb(1);
			}

			m_phase = 0;
		}
		--m_icount;
	}
}
