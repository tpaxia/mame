// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia

// CPU19 bring-up: register operations and basic byte memory transfers.
// Timing is provisional (one fetch + one execute scheduling quantum).
// Channel operations and interrupts use bus callbacks; DMA waits are absent.
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
	, m_irq_ack_cb(*this)
	, m_irq_end_cb(*this)
	, m_command_cb(*this)
	, m_strobe_cb(*this)
	, m_control_cb(*this)
	, m_service_console_cb(*this)
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
	save_item(NAME(m_core.ecorn));
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
	return std::make_unique<puce_disassembler>();
}

void puce_device::execute_run()
{
	while (m_icount > 0)
	{
		if (m_stopped) { m_icount = 0; return; }
		if (m_phase == 0)
		{
			const u8 irq=m_irq_request_cb(m_core.level);
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
			if (m_ir == 0xbd00 && previous_level != 4) m_irq_end_cb(previous_level);
			if (!done)
				done = m_core.execute_word(m_ir,
					[this] (u16 address) { return m_program.read_word(address); },
					[this] (u16 address, u16 value) { m_program.write_word(address, value); });
			const unsigned x = (m_ir >> 4) & 15;
			if (!done)
			{
				switch ((m_ir & 0xff00) == 0xb200 ? 0xb20f : (m_ir & 0xff0f))
				{
				case 0xaa00: case 0xb900: case 0xb20f:
					done = m_core.execute_input(m_ir, m_name_type_cb(m_core.level), 0);
					break;
				case 0xb808: case 0xa908:
					done = m_core.execute_input(m_ir, 0, m_input_data_cb(m_core.level));
					break;
				}
			}
			if (!done)
				done = m_core.execute_byte(m_ir,
					[this] (u16 address) { return m_program.read_word(address >> 1, puce_state::byte_mask(address)) >> puce_state::byte_shift(address); },
					[this] (u16 address, u8 value) { m_program.write_word(address >> 1, u16(value) << puce_state::byte_shift(address), puce_state::byte_mask(address)); });
			if (!done && (m_ir >> 8) == 0xfa)
			{
				// CPU19 V2 p.2.108: separate SUCE2 service-console bus.
				m_service_console_cb((u16(m_core.b(m_ir & 15)) << 8) | m_core.a(x)); done=true;
			}
			if (!done && (m_ir == 0xbd70 || m_ir == 0xbd80))
			{
				m_control_cb(m_core.level, m_ir == 0xbd70 ? 7 : 8); done=true;
			}
			if (!done && (m_ir & 0xff0f) == 0xad0f)
			{
				m_core.l[x]=(m_core.l[x]&0xf000)|((m_core.l[x]-1)&0x0fff);
				if (!(m_core.l[x]&0x0fff)) m_control_cb(m_core.level, 0); // ECOF pulse
				done=true;
			}
			if (!done)
			{
				const unsigned code=m_ir&0xff0f;
				const bool input=code==0x8d08 || code==0xa108 || code==0xa208;
				const bool output=code==0x9000 || code==0x9d00 || code==0x9400;
				if (input || output)
				{
					const u16 address=m_core.indirect(x);
					const int delta=(code==0xa108 || code==0x9d00) ? -1 : (code==0xa208 || code==0x9400) ? 1 : 0;
					if (x<12) m_core.l[x]+=delta; else m_core.set_a(x,m_core.a(x)+delta);
					if (input)
					{
						m_program.write_word(address>>1,u16(m_input_data_cb(m_core.level))<<puce_state::byte_shift(address),puce_state::byte_mask(address));
						m_strobe_cb(m_core.level);
					}
					else m_data_cb(m_core.level,m_program.read_word(address>>1,puce_state::byte_mask(address))>>puce_state::byte_shift(address));
					done=true;
				}
			}
			if (!done && m_ir == 0xbd40)
			{
				m_strobe_cb(m_core.level); done=true;
			}
			if (!done && (m_ir & 0xff0f) == 0xb402)
			{
				const u16 address=m_core.indirect(x);
				m_command_cb(m_core.level,m_program.read_word(address>>1,puce_state::byte_mask(address))>>puce_state::byte_shift(address));
				done=true;
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
