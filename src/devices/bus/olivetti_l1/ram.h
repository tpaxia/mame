// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia

#ifndef MAME_BUS_OLIVETTI_L1_RAM_H
#define MAME_BUS_OLIVETTI_L1_RAM_H

#pragma once

#include "l1.h"

#include "machine/ram.h"

class olivetti_l1_ram_device : public device_t, public device_olivetti_l1_card_interface
{
public:
	olivetti_l1_ram_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock = 0);

	virtual bool io_responds() const override { return false; }
	virtual bool memory_claims(offs_t address) const override;
	virtual u8 memory_r(offs_t address) override;
	virtual void memory_w(offs_t address, u8 data) override;
	virtual bool is_ram() const override { return true; }
	virtual u32 ram_capacity() const override { return m_capacity; }
	virtual void configure_ram(u32 base, u32 backing_offset, u32 size) override;

protected:
	olivetti_l1_ram_device(machine_config const &mconfig, device_type type, char const *tag, device_t *owner, u32 clock, u32 capacity);

	virtual void device_start() override ATTR_COLD;

private:
	u32 size() const;
	u8 *data() const;

	required_device<ram_device> m_ram;
	std::unique_ptr<u8[]> m_memory;
	u32 m_base = 0x010000;
	u32 m_backing_offset = 0;
	u32 m_capacity = 0;
	u32 m_size = 0;
};

#define DECLARE_OLIVETTI_L1_RAM_BOARD(_class) \
class _class : public olivetti_l1_ram_device \
{ \
public: \
	_class(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock = 0); \
};

DECLARE_OLIVETTI_L1_RAM_BOARD(olivetti_l1_me256k_device)
DECLARE_OLIVETTI_L1_RAM_BOARD(olivetti_l1_me384k_device)
DECLARE_OLIVETTI_L1_RAM_BOARD(olivetti_l1_me512k_device)
DECLARE_OLIVETTI_L1_RAM_BOARD(olivetti_l1_ra57d_device)
DECLARE_OLIVETTI_L1_RAM_BOARD(olivetti_l1_ra57e_device)
DECLARE_OLIVETTI_L1_RAM_BOARD(olivetti_l1_ra57c_device)
DECLARE_OLIVETTI_L1_RAM_BOARD(olivetti_l1_ra57b_device)
DECLARE_OLIVETTI_L1_RAM_BOARD(olivetti_l1_ra57a_device)

#undef DECLARE_OLIVETTI_L1_RAM_BOARD

DECLARE_DEVICE_TYPE(OLIVETTI_L1_RAM, olivetti_l1_ram_device)
DECLARE_DEVICE_TYPE(OLIVETTI_L1_ME256K, olivetti_l1_me256k_device)
DECLARE_DEVICE_TYPE(OLIVETTI_L1_ME384K, olivetti_l1_me384k_device)
DECLARE_DEVICE_TYPE(OLIVETTI_L1_ME512K, olivetti_l1_me512k_device)
DECLARE_DEVICE_TYPE(OLIVETTI_L1_RA57D, olivetti_l1_ra57d_device)
DECLARE_DEVICE_TYPE(OLIVETTI_L1_RA57E, olivetti_l1_ra57e_device)
DECLARE_DEVICE_TYPE(OLIVETTI_L1_RA57C, olivetti_l1_ra57c_device)
DECLARE_DEVICE_TYPE(OLIVETTI_L1_RA57B, olivetti_l1_ra57b_device)
DECLARE_DEVICE_TYPE(OLIVETTI_L1_RA57A, olivetti_l1_ra57a_device)

#endif // MAME_BUS_OLIVETTI_L1_RAM_H
