// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#ifndef MAME_BUS_P6066_HDU_H
#define MAME_BUS_P6066_HDU_H
#pragma once
#include "imagedev/harddriv.h"

// Healthy sector media: CHD stores payloads; physical IDs are derived.
struct p6066_hdu_sector
{
 u8 cylinder=0, sector=0;
 bool id_present=false, id_crc_valid=false, data_present=false, data_crc_valid=false;
 std::array<u8,256> data{};
};
class p6066_hdu_device : public harddisk_image_device
{
public:
 p6066_hdu_device(const machine_config &, const char *, device_t *, u32 clock = 0);
 static constexpr unsigned CYLINDERS=202, HEADS=4, SECTORS=48, SLOTS=49, SECTOR_BYTES=256;
 static constexpr unsigned USER_BYTES=200*HEADS*SECTORS*SECTOR_BYTES;
 bool ready() const { return bool(m_hard_disk_handle); }
 unsigned cylinder() const { return m_cylinder; }
 void seek(unsigned cylinder);
 p6066_hdu_sector sector(unsigned head,unsigned slot);
 bool record_format(unsigned head,unsigned slot,const std::array<u8,20> &data,bool intact);
 bool record_data(unsigned head,unsigned slot,const std::array<u8,256> &data,bool crc_valid);
 virtual std::pair<std::error_condition,std::string> call_load() override;
 virtual const char *file_extensions() const noexcept override { return "chd"; }
protected:
 virtual void device_start() override;
private:
 unsigned lba(unsigned head,unsigned slot) const { return (m_cylinder*HEADS+head)*SECTORS+slot; }
 u16 m_cylinder=0;
};
DECLARE_DEVICE_TYPE(P6066_HDU, p6066_hdu_device)
#endif
