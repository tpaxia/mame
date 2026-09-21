// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#include "emu.h"
#include "hdu.h"
DEFINE_DEVICE_TYPE(P6066_HDU,p6066_hdu_device,"p6066_hdu2110","Olivetti HDU 2110 (10 MB)")
p6066_hdu_device::p6066_hdu_device(const machine_config &mconfig,const char *tag,device_t *owner,u32 clock)
 : harddisk_image_device(mconfig,P6066_HDU,tag,owner,clock)
{
 set_interface("p6066_hdu");
}
void p6066_hdu_device::device_start()
{
 harddisk_image_device::device_start();
 save_item(NAME(m_cylinder));
}
std::pair<std::error_condition,std::string> p6066_hdu_device::call_load()
{
 auto result=harddisk_image_device::call_load();
 if(result.first) return result;
 const auto &info=get_info();
 if(!m_chd || info.cylinders!=CYLINDERS || info.heads!=HEADS || info.sectors!=SECTORS || info.sectorbytes!=SECTOR_BYTES)
 {
  harddisk_image_device::call_unload();
  return {image_error::INVALIDIMAGE,"HDU2110 requires a CHD with CHS 202,4,48 and 256-byte sectors"};
 }
 return result;
}
void p6066_hdu_device::seek(unsigned cylinder)
{
 if(cylinder>=CYLINDERS) fatalerror("HDU2110 invalid cylinder requires actuator evidence");
 m_cylinder=cylinder;
}
p6066_hdu_sector p6066_hdu_device::sector(unsigned head,unsigned slot)
{
 if(head>=HEADS||slot>=SLOTS) fatalerror("HDU2110 invalid physical sector");
 p6066_hdu_sector result;
 if(!ready() || slot==SECTORS) return result; // spare slot has no normal ID
 result.cylinder=m_cylinder;result.sector=head*SECTORS+slot;
 result.id_present=result.id_crc_valid=true;
 result.data_present=read(lba(head,slot),result.data.data());
 result.data_crc_valid=result.data_present;
 return result;
}
bool p6066_hdu_device::record_data(unsigned head,unsigned slot,const std::array<u8,256> &data,bool crc_valid)
{
 if(head>=HEADS||slot>=SLOTS) fatalerror("HDU2110 invalid physical sector write");
 // CHD retains payload only. Controller timing/CRC errors are still reported
 // by DIFO, but are not persistent media defects in this healthy-disk model.
 return ready() && slot<SECTORS && write(lba(head,slot),data.data());
}
bool p6066_hdu_device::record_format(unsigned head,unsigned slot,const std::array<u8,20> &data,bool intact)
{
 if(head>=HEADS||slot>=SLOTS) fatalerror("HDU2110 invalid format slot");
 if(!ready() || !intact) return false;
 if(slot==SECTORS) return std::all_of(data.begin(),data.end(),[](u8 v){return v==0xff;});
 // Ordinary physical formatting is allowed but unnecessary. CHD cannot
 // represent relocated/malformed IDs or guest-supplied raw CRC templates.
 if(data[5]!=0x55 || data[17]!=0x55 || data[6]!=m_cylinder || data[7]!=head*SECTORS+slot) return false;
 std::array<u8,256> blank;blank.fill(0xff);
 return write(lba(head,slot),blank.data());
}
