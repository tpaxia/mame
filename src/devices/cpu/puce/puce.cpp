// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia

// CPU19/CPU19M instruction-level execution.
// Timing is provisional (one fetch + one execute scheduling quantum).
// All documented canonical instructions have execution paths. Channel
// transactions use bus callbacks; full interrupt/fault arbitration and DMA
// waits remain under audit. See docs/p6066/instruction-audit.md.
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
	, m_ecorn_cb(*this)
	, m_name_type_cb(*this, 0)
	, m_input_data_cb(*this, 0)
	, m_irq_request_cb(*this, 0)
	, m_interrupt_sync_cb(*this)
	, m_irq_ack_cb(*this)
	, m_irq_end_cb(*this)
	, m_strobe_cb(*this)
	, m_control_cb(*this)
	, m_command_cb(*this)
	, m_service_console_cb(*this)
	, m_service_console_input_cb(*this, 0)
	, m_service_console_control_cb(*this)
	, m_ecof_cb(*this, 0)
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
	save_item(NAME(m_core.internal_name));
	save_item(NAME(m_core.com1_pending));
	save_item(NAME(m_core.active));
	save_item(NAME(m_core.ecorn));
	save_item(NAME(m_core.inhibit_level3));
	machine().save().register_postload(save_prepost_delegate(FUNC(puce_device::update_ecorn), this));
	save_item(NAME(m_ir));
	save_item(NAME(m_fetch_pc));
	save_item(NAME(m_phase));
	save_item(NAME(m_stopped));
	save_item(NAME(m_invalid_pending));
	save_item(NAME(m_invalid_cycles));
	state_add(6, "INVALID", m_invalid_cycles).readonly();
	state_add(STATE_GENPC, "PC", m_debug_pc).callimport().callexport();
	state_add(STATE_GENPCBASE, "CURPC", m_fetch_pc).noshow();
	state_add(STATE_GENFLAGS, "DI", m_core.di);
	state_add(5, "ECORN", m_core.ecorn).readonly();
	state_add(1, "LEVEL", m_core.level).readonly();
	state_add(2, "IR", m_ir).readonly();
	state_add(4, "STOPPED", m_stopped).readonly();
	state_add(3, "PHASE", m_phase).readonly();
	for (unsigned i = 0; i != 16; ++i)
		state_add(16 + i, util::string_format("L%u", i).c_str(), m_core.l[i]);
}

void puce_device::update_ecorn()
{
	m_ecorn_cb(m_core.ecorn);
}

void puce_device::device_reset()
{
	m_core.reset();
	update_ecorn();
	m_stopped = false;
	m_invalid_pending = false;
	m_invalid_cycles = 0;
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
	return std::make_unique<puce_disassembler>(m_core.cpu19m);
}

// All channel instruction ordering is shared with the ROM-free tests.
struct puce_device::channel_adapter
{
	puce_device &cpu;
	// GOINO fig.1.2 / printed p.12: SETTO masks direct selection from
	// external ECC10/ECC20/ECCA0/ECCB0 acknowledgements. Internal COM1/INV
	// uses L1 without asserting those pins, so the external channel retains
	// direct selection. Nested external levels 1/2 still use their owner.
	unsigned channel_level() const
	{
		return cpu.m_core.external_channel_level();
	}
	u16 read_word(u16 address) { return cpu.m_program.read_word(address); }
	void write_word(u16 address, u16 value) { cpu.m_program.write_word(address, value); }
	u8 read_byte(u16 address) { return cpu.m_program.read_word(address >> 1, puce_state::byte_mask(address)) >> puce_state::byte_shift(address); }
	void write_byte(u16 address, u8 value) { cpu.m_program.write_word(address >> 1, u16(value) << puce_state::byte_shift(address), puce_state::byte_mask(address)); }
	u16 name_type() { return cpu.m_name_type_cb(channel_level()); }
	u8 input() { return cpu.m_input_data_cb(channel_level()); }
	void output(u16 value, u16 mask) { cpu.m_data_cb(channel_level(), value, mask); }
	void command(u16 value, u16 mask) { cpu.m_command_cb(channel_level(), value, mask); }
	void select(u8 value) { cpu.m_select_cb(value); }
	void strobe() { cpu.m_strobe_cb(channel_level()); }
	void control(u8 value) { cpu.m_control_cb(channel_level(), value); }
	void console_output(u16 value) { cpu.m_service_console_cb(value); }
	u8 console_input(u8 selector)
	{
		if (cpu.m_service_console_input_cb.isunset()) fatalerror("PUCE: SUCE2 input bus is not connected");
		return cpu.m_service_console_input_cb(selector);
	}
	void console_control(u8 command)
	{
		// SUCE2 is an optional external console: unconnected command outputs
		// have no receiver. Input pull levels require separate board evidence.
		cpu.m_service_console_control_cb(command);
	}
	bool ecof()
	{
		if (cpu.m_ecof_cb.isunset()) fatalerror("PUCE: SADE ECOFO source is not connected");
		return cpu.m_ecof_cb();
	}
};

void puce_device::execute_run()
{
	while (m_icount > 0)
	{
		if (m_stopped) { m_icount = 0; return; }
		if (m_phase == 0)
		{
			// US4032895 table 16: ALFA strobes only higher levels.
			m_interrupt_sync_cb(((1U << m_core.level) - 1) & 0x0e);
			const u8 irq=m_irq_request_cb(m_core.external_irq_poll_level());
			if (irq && (!m_invalid_pending || irq<=2))
			{
				const unsigned level=irq<=2 ? irq : 3;
				if (!m_core.enter_level(level)) fatalerror("PUCE: invalid interrupt priority");
				m_irq_ack_cb(irq);
			}
			else if (m_invalid_pending && m_core.level == 4)
			{
				// Transaction-level INV00 recovery. Circuit timing and faults
				// during higher-priority service still require verification.
				m_invalid_pending = false;
				if (!m_core.enter_level(3))
					fatalerror("PUCE: invalid memory cycle at level %u is not implemented", m_core.level);
				m_core.internal_name = 3;
			}
			else if (m_core.com1_pending && m_core.level == 4)
			{
				// CPU19M pp.3.13-3.14: COM1 is below INV and 3A/3B.
				// Requesting service must not change the current level during
				// BETA, before the shared ALFA arbitration can choose a source.
				m_core.enter_level(3);
				m_core.com1_pending = false;
				m_core.internal_name = 2;
			}
			m_fetch_pc = m_core.pc();
			debugger_instruction_hook(m_fetch_pc);
			// An older invalid data cycle can remain pending while level 1/2
			// runs. Only a new fault from this read is an invalid fetch.
			const u32 invalid_before_fetch = m_invalid_cycles;
			m_ir = m_program.read_word(m_fetch_pc);
			if (m_invalid_cycles != invalid_before_fetch) fatalerror("PUCE: invalid instruction fetch at %04X is not implemented", m_fetch_pc);
			m_core.advance();
			m_phase = 1;
		}
		else
		{
			const bool previous_ecorn = m_core.ecorn;
			const unsigned previous_level = m_core.level;
			bool done = m_core.execute_register(m_ir);
			if (m_core.ecorn != previous_ecorn) update_ecorn();
			if (m_ir == 0xbd00 && previous_level != 4)
			{
				// COM0 also strobes the ending level, allowing cleared source
				// latches to propagate before ownership is released.
				m_interrupt_sync_cb(((1U << (previous_level + 1)) - 1) & 0x0e);
				m_irq_end_cb(previous_level);
			}
			if (!done)
				done = m_core.execute_word(m_ir,
					[this] (u16 address) { return m_program.read_word(address); },
					[this] (u16 address, u16 value) { m_program.write_word(address, value); });
			if (!done)
				done = m_core.execute_byte(m_ir,
					[this] (u16 address) { return m_program.read_word(address >> 1, puce_state::byte_mask(address)) >> puce_state::byte_shift(address); },
					[this] (u16 address, u8 value) { m_program.write_word(address >> 1, u16(value) << puce_state::byte_shift(address), puce_state::byte_mask(address)); });
			if (!done)
			{
				channel_adapter channel{*this};
				done = m_core.execute_channel(m_ir, channel);
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
