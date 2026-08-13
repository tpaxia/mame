// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia

#include "emu.h"
#include "l1.h"

olivetti_l1_slot_device::olivetti_l1_slot_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock)
	: device_t(mconfig, OLIVETTI_L1_SLOT, tag, owner, clock)
	, device_single_card_slot_interface<device_olivetti_l1_card_interface>(mconfig, *this)
	, m_bus(*this, finder_base::DUMMY_TAG)
{
}

void olivetti_l1_slot_device::device_resolve_objects()
{
	device_olivetti_l1_card_interface *const card = get_card_device();
	if (card)
	{
		card->m_bus = m_bus;
		card->m_position = m_position;
		card->m_select = m_select;
	}
	m_bus->add_slot(m_position, m_select, card);
}

void olivetti_l1_slot_device::device_start()
{
}

olivetti_l1_bus_device::olivetti_l1_bus_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock)
	: device_t(mconfig, OLIVETTI_L1_BUS, tag, owner, clock)
	, m_ram(*this, ":" RAM_TAG)
{
}

void olivetti_l1_bus_device::device_start()
{
	if (!m_ram->started())
		throw device_missing_dependencies();

	validate_layout();

	u32 base = 0x010000;
	u32 backing_offset = 0;
	unsigned ram_cards = 0;
	bool const explicit_ram = std::any_of(m_chain.begin(), m_chain.end(),
		[](device_olivetti_l1_card_interface const *card) { return card->is_ram() && card->ram_capacity(); });

	for (device_olivetti_l1_card_interface *const card : m_chain)
		if (card->is_ram() && (!explicit_ram || card->ram_capacity()))
			ram_cards++;

	for (device_olivetti_l1_card_interface *const card : m_chain)
	{
		if (!card->is_ram())
			continue;
		u32 const capacity = card->ram_capacity();
		if (explicit_ram && !capacity)
		{
			card->configure_ram(base, backing_offset, 0);
			continue;
		}
		u32 const size = capacity ? capacity : m_ram->size();
		if (size > 0x01000000 - base)
			throw emu_fatalerror("Olivetti L1 RAM population exceeds the 24-bit physical address space at position %u", card->position() + 1);
		card->configure_ram(base, backing_offset, size);
		base += size;
		backing_offset += size;
	}
	if (!ram_cards)
		throw emu_fatalerror("Olivetti L1 chassis has no RAM board");

	save_item(NAME(m_vi_state));
	save_item(NAME(m_busreq_state));
}

void olivetti_l1_bus_device::device_reset()
{
	m_vi_state = 0;
	m_busreq_state = 0;
	update_vi();
	update_busreq();
}

void olivetti_l1_bus_device::add_slot(u8 position, u8 select, device_olivetti_l1_card_interface *card)
{
	if (position >= m_positions.size())
		throw emu_fatalerror("Olivetti L1 connector has invalid physical position %u", position + 1);
	if (m_connectors[position])
		throw emu_fatalerror("Olivetti L1 physical position %u is registered twice", position + 1);
	m_connectors[position] = true;
	m_positions[position] = card;

	if (!card)
		return;
	if (select >= m_cards.size())
		throw emu_fatalerror("Olivetti L1 card %s has invalid select %u", card->device().tag(), select);
	if (card->io_responds() && m_cards[select])
		throw emu_fatalerror("Olivetti L1 select %u is used by both %s and %s", select, m_cards[select]->device().tag(), card->device().tag());

	if (card->io_responds())
		m_cards[select] = card;
	if (auto *const cpu = dynamic_cast<device_olivetti_l1_cpu_card_interface *>(card))
	{
		if (m_cpu)
			throw emu_fatalerror("Olivetti L1 bus has CPU cards in both %s and %s", m_cpu->device().tag(), card->device().tag());
		m_cpu = cpu;
	}
	auto const at = std::lower_bound(m_chain.begin(), m_chain.end(), position,
		[](device_olivetti_l1_card_interface const *entry, u8 value) { return entry->position() < value; });
	m_chain.insert(at, card);
}

void olivetti_l1_bus_device::validate_layout()
{
	u8 const positions = (m_chassis == chassis::m30_m34) ? 9 : 14;
	u8 const cpu_position = (m_chassis == chassis::m30_m34) ? 1 : 0;
	u8 const first_ram_position = (m_chassis == chassis::m30_m34) ? 0 : 1;
	char const *const model = (m_chassis == chassis::m30_m34) ? "M30/M34" : "M40/M44";

	for (u8 position = 0; position < positions; position++)
		if (!m_connectors[position])
			throw emu_fatalerror("Olivetti %s chassis is missing physical connector %u", model, position + 1);

	if (!m_cpu || m_cpu->position() != cpu_position)
		throw emu_fatalerror("Olivetti %s CPU board must occupy physical position %u", model, cpu_position + 1);

	device_olivetti_l1_card_interface const *const first_ram = m_positions[first_ram_position];
	if (!first_ram || !first_ram->is_ram())
		throw emu_fatalerror("Olivetti %s first RAM board must occupy physical position %u", model, first_ram_position + 1);

	bool found_empty = false;
	u8 empty_position = 0;
	for (u8 position = cpu_position + 1; position < positions; position++)
	{
		if (!m_positions[position])
		{
			if (!found_empty)
				empty_position = position;
			found_empty = true;
		}
		else if (found_empty)
		{
			throw emu_fatalerror(
				"Invalid Olivetti %s card-cage layout: physical position %u is populated after empty position %u; boards must be contiguous from the CPU",
				model, position + 1, empty_position + 1);
		}
	}
}

u16 olivetti_l1_bus_device::io_r(offs_t offset, u16 mem_mask)
{
	u32 const address = offset << 1;
	u8 const selected_slot = BIT(address, 12, 4);
	device_olivetti_l1_card_interface *const selected = card(selected_slot);
	if (!selected)
	{
		if (m_cpu)
			m_cpu->ready_fault_w(ASSERT_LINE);
		return 0xffff;
	}

	u16 result = 0xffff;
	if (ACCESSING_BITS_8_15)
		result = (result & 0x00ff) | (u16(selected->io_r(address & 0xff)) << 8);
	if (ACCESSING_BITS_0_7)
		result = (result & 0xff00) | selected->io_r((address + 1) & 0xff);
	return result;
}

void olivetti_l1_bus_device::io_w(offs_t offset, u16 data, u16 mem_mask)
{
	u32 const address = offset << 1;
	u8 const selected_slot = BIT(address, 12, 4);
	device_olivetti_l1_card_interface *const selected = card(selected_slot);
	if (!selected)
	{
		if (m_cpu)
			m_cpu->ready_fault_w(ASSERT_LINE);
		return;
	}

	if (ACCESSING_BITS_8_15)
		selected->io_w(address & 0xff, data >> 8);
	if (ACCESSING_BITS_0_7)
		selected->io_w((address + 1) & 0xff, data & 0xff);
}

void olivetti_l1_bus_device::vi_w(u8 select, int state)
{
	if (state)
		m_vi_state |= u16(1) << select;
	else
		m_vi_state &= ~(u16(1) << select);
	update_vi();
}

void olivetti_l1_bus_device::busreq_w(u8 select, int state)
{
	if (state)
		m_busreq_state |= u16(1) << select;
	else
		m_busreq_state &= ~(u16(1) << select);
	update_busreq();
}

void olivetti_l1_bus_device::update_vi()
{
	if (m_cpu)
		m_cpu->bus_vi_w(vi_pending() ? ASSERT_LINE : CLEAR_LINE);
}

bool olivetti_l1_bus_device::vi_pending() const
{
	if (m_vi_state)
		return true;
	if (!m_cpu)
		return false;
	return m_cpu->local_vi_pending(interrupt_level::l1a)
		|| m_cpu->local_vi_pending(interrupt_level::l1b)
		|| m_cpu->local_vi_pending(interrupt_level::l2);
}

void olivetti_l1_bus_device::update_busreq()
{
	device_olivetti_l1_card_interface *winner = nullptr;
	u8 winner_distance = 0xff;
	if (m_cpu)
	{
		for (device_olivetti_l1_card_interface *const card : m_chain)
		{
			if (card == m_cpu || !BIT(m_busreq_state, card->select()))
				continue;
			u8 const distance = (card->position() > m_cpu->position())
				? card->position() - m_cpu->position()
				: m_cpu->position() - card->position();
			if (distance < winner_distance)
			{
				winner = card;
				winner_distance = distance;
			}
		}
	}

	if (m_cpu)
		m_cpu->bus_request_w(m_busreq_state ? ASSERT_LINE : CLEAR_LINE);
	for (device_olivetti_l1_card_interface *const card : m_chain)
		if (card != m_cpu)
			card->bus_grant_w(card == winner ? ASSERT_LINE : CLEAR_LINE);
}

bool olivetti_l1_bus_device::memory_r(offs_t address, u8 &data)
{
	device_olivetti_l1_card_interface *responder = nullptr;
	for (device_olivetti_l1_card_interface *const card : m_chain)
	{
		if (!card->memory_claims(address))
			continue;
		if (responder)
			throw emu_fatalerror("Olivetti L1 physical address %06X is decoded by both %s and %s", unsigned(address), responder->device().tag(), card->device().tag());
		responder = card;
	}
	if (!responder)
		return false;
	data = responder->memory_r(address);
	return true;
}

bool olivetti_l1_bus_device::memory_w(offs_t address, u8 data)
{
	device_olivetti_l1_card_interface *responder = nullptr;
	for (device_olivetti_l1_card_interface *const card : m_chain)
	{
		if (!card->memory_claims(address))
			continue;
		if (responder)
			throw emu_fatalerror("Olivetti L1 physical address %06X is decoded by both %s and %s", unsigned(address), responder->device().tag(), card->device().tag());
		responder = card;
	}
	if (!responder)
		return false;
	responder->memory_w(address, data);
	return true;
}

u8 olivetti_l1_bus_device::physical_r(offs_t address)
{
	u8 data;
	return memory_r(address, data) ? data : 0xff;
}

void olivetti_l1_bus_device::physical_w(offs_t address, u8 data)
{
	memory_w(address, data);
}

u16 olivetti_l1_bus_device::viack_r()
{
	auto const pending = [this](device_olivetti_l1_card_interface *card, interrupt_level level)
	{
		if (card == m_cpu)
			return m_cpu->local_vi_pending(level);
		return card->vi_level() == level && BIT(m_vi_state, card->select());
	};
	auto const acknowledge = [this](device_olivetti_l1_card_interface *card, interrupt_level level)
	{
		return (card == m_cpu) ? m_cpu->local_viack_r(level) : card->viack_r();
	};

	for (interrupt_level const level : { interrupt_level::l1a, interrupt_level::l1b, interrupt_level::l2 })
	{
		if (level == interrupt_level::l1b)
		{
			for (auto card = m_chain.rbegin(); card != m_chain.rend(); ++card)
				if (pending(*card, level))
					return acknowledge(*card, level);
		}
		else
		{
			for (device_olivetti_l1_card_interface *const card : m_chain)
				if (pending(card, level))
					return acknowledge(card, level);
		}
	}
	return 0;
}

device_olivetti_l1_card_interface::device_olivetti_l1_card_interface(machine_config const &mconfig, device_t &device)
	: device_interface(device, "olivetti_l1")
{
}

device_olivetti_l1_card_interface::~device_olivetti_l1_card_interface()
{
}

device_olivetti_l1_cpu_card_interface::device_olivetti_l1_cpu_card_interface(machine_config const &mconfig, device_t &device)
	: device_olivetti_l1_card_interface(mconfig, device)
{
}

device_olivetti_l1_cpu_card_interface::~device_olivetti_l1_cpu_card_interface()
{
}

void device_olivetti_l1_card_interface::interface_pre_start()
{
	if (!m_bus)
		throw device_missing_dependencies();
}

DEFINE_DEVICE_TYPE(OLIVETTI_L1_BUS, olivetti_l1_bus_device, "olivetti_l1_bus", "Olivetti L1 backplane")
DEFINE_DEVICE_TYPE(OLIVETTI_L1_SLOT, olivetti_l1_slot_device, "olivetti_l1_slot", "Olivetti L1 backplane slot")
