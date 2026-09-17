// license:BSD-3-Clause
// copyright-holders:P6066 contributors

// Development-only P6066 CPU bring-up configuration.
// The merged reference CAROM is verified as an analysis input, but physical
// chip mapping/revision and installed RAM population remain unresolved.
// No console, floppy, DMA or serial boards yet; execution stops on first I/O.

#include "emu.h"
#include "cpu/puce/puce.h"

namespace {

class p6066_state : public driver_device
{
public:
	p6066_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag), m_maincpu(*this, "maincpu") { }
	void p6066(machine_config &config);

private:
	required_device<puce_device> m_maincpu;
	void memory_map(address_map &map);
};

void p6066_state::memory_map(address_map &map)
{
	// Word addresses. Provisional lower 64KB RAM for register/memory tests;
	// this is not yet a verified physical RAM-board configuration.
	map(0x0000, 0x7fff).ram();
	map(0x8000, 0x87ff).rom().region("carom", 0);
}

void p6066_state::p6066(machine_config &config)
{
	PUCE(config, m_maincpu, 1'000'000); // provisional scheduling rate, NOT verified hardware clock
	m_maincpu->set_addrmap(AS_PROGRAM, &p6066_state::memory_map);
}

static INPUT_PORTS_START(p6066)
INPUT_PORTS_END

ROM_START(p6066)
	ROM_REGION16_BE(0x1000, "carom", 0)
	// Merged reference from dthierbach/olivetti-p6060; not physical chip dumps.
	ROM_LOAD("carom.bin", 0, 0x1000, CRC(9caca305) SHA1(63a68b4787f33bef156f05c6b50269357d0d3c2f))
ROM_END

} // anonymous namespace

COMP(19??, p6066, 0, 0, p6066, p6066, p6066_state, empty_init, "Olivetti", "P6066 (CPU bring-up)", MACHINE_NOT_WORKING | MACHINE_NO_SOUND)
