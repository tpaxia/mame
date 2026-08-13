// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia

#include "emu.h"
#include "ram.h"

DEFINE_DEVICE_TYPE(OLIVETTI_L1_RAM,    olivetti_l1_ram_device,    "olivetti_l1_ram",    "Olivetti L1 automatic RAM population")
DEFINE_DEVICE_TYPE(OLIVETTI_L1_ME256K, olivetti_l1_me256k_device, "olivetti_l1_me256k", "Olivetti ME027-32 256 KB RAM board")
DEFINE_DEVICE_TYPE(OLIVETTI_L1_ME384K, olivetti_l1_me384k_device, "olivetti_l1_me384k", "Olivetti ME027-32 384 KB RAM board")
DEFINE_DEVICE_TYPE(OLIVETTI_L1_ME512K, olivetti_l1_me512k_device, "olivetti_l1_me512k", "Olivetti ME027-32 512 KB RAM board")
DEFINE_DEVICE_TYPE(OLIVETTI_L1_RA57D,  olivetti_l1_ra57d_device,  "olivetti_l1_ra57d",  "Olivetti RA57/D 512 KB RAM board")
DEFINE_DEVICE_TYPE(OLIVETTI_L1_RA57E,  olivetti_l1_ra57e_device,  "olivetti_l1_ra57e",  "Olivetti RA57/E 512 KB RAM board")
DEFINE_DEVICE_TYPE(OLIVETTI_L1_RA57C,  olivetti_l1_ra57c_device,  "olivetti_l1_ra57c",  "Olivetti RA57/C 1 MB RAM board")
DEFINE_DEVICE_TYPE(OLIVETTI_L1_RA57B,  olivetti_l1_ra57b_device,  "olivetti_l1_ra57b",  "Olivetti RA57/B 1.5 MB RAM board")
DEFINE_DEVICE_TYPE(OLIVETTI_L1_RA57A,  olivetti_l1_ra57a_device,  "olivetti_l1_ra57a",  "Olivetti RA57/A 2 MB RAM board")

olivetti_l1_ram_device::olivetti_l1_ram_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock)
	: olivetti_l1_ram_device(mconfig, OLIVETTI_L1_RAM, tag, owner, clock, 0)

{
}

olivetti_l1_ram_device::olivetti_l1_ram_device(machine_config const &mconfig, device_type type, char const *tag, device_t *owner, u32 clock, u32 capacity)
	: device_t(mconfig, type, tag, owner, clock)
	, device_olivetti_l1_card_interface(mconfig, *this)
	, m_ram(*this, ":" RAM_TAG)
	, m_capacity(capacity)
{
}

#define DEFINE_OLIVETTI_L1_RAM_BOARD(_class, _type, _capacity) \
_class::_class(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock) \
	: olivetti_l1_ram_device(mconfig, _type, tag, owner, clock, _capacity) \
{ \
}

DEFINE_OLIVETTI_L1_RAM_BOARD(olivetti_l1_me256k_device, OLIVETTI_L1_ME256K, 256 * 1024)
DEFINE_OLIVETTI_L1_RAM_BOARD(olivetti_l1_me384k_device, OLIVETTI_L1_ME384K, 384 * 1024)
DEFINE_OLIVETTI_L1_RAM_BOARD(olivetti_l1_me512k_device, OLIVETTI_L1_ME512K, 512 * 1024)
DEFINE_OLIVETTI_L1_RAM_BOARD(olivetti_l1_ra57d_device,  OLIVETTI_L1_RA57D,  512 * 1024)
DEFINE_OLIVETTI_L1_RAM_BOARD(olivetti_l1_ra57e_device,  OLIVETTI_L1_RA57E,  512 * 1024)
DEFINE_OLIVETTI_L1_RAM_BOARD(olivetti_l1_ra57c_device,  OLIVETTI_L1_RA57C,  1024 * 1024)
DEFINE_OLIVETTI_L1_RAM_BOARD(olivetti_l1_ra57b_device,  OLIVETTI_L1_RA57B,  1536 * 1024)
DEFINE_OLIVETTI_L1_RAM_BOARD(olivetti_l1_ra57a_device,  OLIVETTI_L1_RA57A,  2048 * 1024)

#undef DEFINE_OLIVETTI_L1_RAM_BOARD

void olivetti_l1_ram_device::device_start()
{
	if (m_capacity)
	{
		m_memory = make_unique_clear<u8[]>(m_capacity);
		save_pointer(NAME(m_memory), m_capacity);
	}
}

void olivetti_l1_ram_device::configure_ram(u32 base, u32 backing_offset, u32 size)
{
	m_base = base;
	m_backing_offset = backing_offset;
	m_size = size;
}

u32 olivetti_l1_ram_device::size() const
{
	if (m_capacity)
		return std::min(m_size, m_capacity);

	u32 const available = (m_backing_offset < m_ram->size()) ? m_ram->size() - m_backing_offset : 0;
	return std::min(m_size, available);
}

u8 *olivetti_l1_ram_device::data() const
{
	return m_capacity ? m_memory.get() : m_ram->pointer() + m_backing_offset;
}

bool olivetti_l1_ram_device::memory_claims(offs_t address) const
{
	address &= 0xffffff;
	return address >= m_base && address - m_base < size();
}

u8 olivetti_l1_ram_device::memory_r(offs_t address)
{
	return data()[(address & 0xffffff) - m_base];
}

void olivetti_l1_ram_device::memory_w(offs_t address, u8 data)
{
	this->data()[(address & 0xffffff) - m_base] = data;
}
