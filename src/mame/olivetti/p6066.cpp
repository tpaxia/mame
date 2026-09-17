// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia

// Development-only P6066 CPU bring-up configuration.
// The merged reference CAROM is verified as an analysis input, but physical
// chip mapping/revision and installed RAM population remain unresolved.
// Partial GOINO/CONDY console; floppy, DMA and serial boards are not present.

#include "emu.h"
#include "cpu/puce/puce.h"
#include "machine/p6066_goino.h"
#include "p6066.lh"

namespace {

class p6066_state : public driver_device
{
public:
	p6066_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag), m_maincpu(*this, "maincpu"), m_console(*this, "console"), m_floppy_select_output(*this, "floppy_selects") { }
	void p6066(machine_config &config);
	INPUT_CHANGED_MEMBER(restart) { if (newval) machine().schedule_soft_reset(); }

protected:
	virtual void machine_start() override
	{
		save_item(NAME(m_floppy_selects));
		machine().save().register_postload(save_prepost_delegate(FUNC(p6066_state::update_outputs), this));
	}
	virtual void machine_reset() override { m_floppy_selects = 0; update_outputs(); }

private:
	u32 m_floppy_selects = 0;
	void update_outputs() { m_floppy_select_output = m_floppy_selects; }
	void select_w(u8 data)
	{
		m_console->select_w(data);
		if (data == 0xe0)
		{
			++m_floppy_selects; update_outputs();
			logerror("CAROM selected floppy controller E0; controller not attached\n");
		}
	}
	required_device<puce_device> m_maincpu;
	required_device<p6066_goino_device> m_console;
	output_finder<> m_floppy_select_output;
	void memory_map(address_map &map);
	u16 missing_memory_r() { if (!machine().side_effects_disabled()) m_maincpu->invalid_memory_access(); return 0; }
	void missing_memory_w(u16 data) { m_maincpu->invalid_memory_access(); }
};

void p6066_state::memory_map(address_map &map)
{
	// Word addresses. Provisional lower 64KB RAM for register/memory tests;
	// this is not yet a verified physical RAM-board configuration.
	map(0x0000, 0x7fff).ram();
	map(0x8000, 0x87ff).rom().region("carom", 0).nopw();
	// ROMCA combines memory-board validity with INV00 (manual p.67).
	// An unclaimed cycle is not a readable bank of zero-filled memory.
	map(0x8800, 0xffff).rw(FUNC(p6066_state::missing_memory_r), FUNC(p6066_state::missing_memory_w));
}

void p6066_state::p6066(machine_config &config)
{
	PUCE(config, m_maincpu, 1'000'000); // provisional scheduling rate, NOT verified hardware clock
	m_maincpu->set_addrmap(AS_PROGRAM, &p6066_state::memory_map);
	m_maincpu->select_cb().set(FUNC(p6066_state::select_w));
	m_maincpu->data_cb().set(m_console, FUNC(p6066_goino_device::data_w));
	m_maincpu->ecorn_cb().set_output("ecorn"); // raw active-low line, not machine reset
	m_maincpu->name_type_cb().set(m_console, FUNC(p6066_goino_device::name_type_r));
	m_maincpu->input_data_cb().set(m_console, FUNC(p6066_goino_device::input_data_r));
	m_maincpu->stopped_cb().set_output("cpu_stopped");
	m_maincpu->set_hold_on_unsupported(true); // keep the console inspectable
	P6066_GOINO(config, m_console);
	screen_device &screen(SCREEN(config, "screen"));
	screen.set_refresh_hz(60); // presentation only; multiplex timing not modelled
	screen.set_size(888, 28);
	screen.set_visarea_full();
	screen.set_screen_update(m_console, FUNC(p6066_goino_device::screen_update));
	config.set_default_layout(layout_p6066);
}

static INPUT_PORTS_START(p6066)
	PORT_START("PANEL")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Restart machine") PORT_CODE(KEYCODE_F3) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(p6066_state::restart), 0)
INPUT_PORTS_END

ROM_START(p6066)
	ROM_REGION16_BE(0x1000, "carom", 0)
	// Merged reference from dthierbach/olivetti-p6060; not physical chip dumps.
	ROM_LOAD("carom.bin", 0, 0x1000, CRC(9caca305) SHA1(63a68b4787f33bef156f05c6b50269357d0d3c2f))
ROM_END

} // anonymous namespace

COMP(19??, p6066, 0, 0, p6066, p6066, p6066_state, empty_init, "Olivetti", "P6066 (CPU bring-up)", MACHINE_NOT_WORKING | MACHINE_NO_SOUND)
