#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""Exercise production HDU image methods with an in-memory image I/O adapter."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
cpp=(ROOT/'src/devices/bus/p6066/hdu.cpp').read_text()
hpp=(ROOT/'src/devices/bus/p6066/hdu.h').read_text()
source=r'''
#include <array>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cassert>
#include <string>
#include <system_error>
#include <stdexcept>
using u8=uint8_t;using u16=uint16_t;
#define BIT(v,n) (((v)>>(n))&1)
void fatalerror(const char *s){throw std::runtime_error(s);}
namespace util{struct option_resolution{};}
namespace image_error{const auto INVALIDIMAGE=std::errc::invalid_argument,UNSPECIFIED=std::errc::io_error;}
'''
a=hpp.index('struct p6066_hdu_sector');source+=hpp[a:hpp.index('\n};',a)+3]+'\n'
source+=r'''
struct harddisk_image_device {
 struct info {unsigned cylinders=202,heads=4,sectors=48,sectorbytes=256;} geometry;
 bool m_chd=true,m_hard_disk_handle=true,fail=false;
 std::vector<u8>file=std::vector<u8>(202*4*48*256,255);
 auto call_load()->std::pair<std::error_condition,std::string>{return {};}
 void call_unload(){m_hard_disk_handle=false;}
 const info &get_info(){return geometry;}
 bool read(unsigned n,void *p){if(fail)return false;std::copy_n(file.data()+n*256,256,(u8*)p);return true;}
 bool write(unsigned n,const void *p){if(fail)return false;std::copy_n((const u8*)p,256,file.data()+n*256);return true;}
};
struct p6066_hdu_device : harddisk_image_device {
 static constexpr unsigned CYLINDERS=202,HEADS=4,SECTORS=48,SLOTS=49,SECTOR_BYTES=256;
 unsigned m_cylinder=0;
 bool ready()const{return m_hard_disk_handle;}
 unsigned lba(unsigned h,unsigned s)const{return (m_cylinder*4+h)*48+s;}
 auto call_load()->std::pair<std::error_condition,std::string>;
 void seek(unsigned);p6066_hdu_sector sector(unsigned,unsigned);
 bool record_data(unsigned,unsigned,const std::array<u8,256>&,bool);
 bool record_format(unsigned,unsigned,const std::array<u8,20>&,bool);
};
'''
for sig in ['std::pair<std::error_condition,std::string> p6066_hdu_device::call_load()',
            'void p6066_hdu_device::seek(', 'p6066_hdu_sector p6066_hdu_device::sector(',
            'bool p6066_hdu_device::record_data(', 'bool p6066_hdu_device::record_format(']:
    start=cpp.index(sig);end=cpp.index('{',start)+1;depth=1
    while depth:
        depth+=(cpp[end]=='{')-(cpp[end]=='}');end+=1
    source+=cpp[start:end]+'\n'
source+=r'''
int main(){
 p6066_hdu_device d;assert(!d.call_load().first);
 for(unsigned c: {0U,199U,200U,201U})for(unsigned h=0;h<4;++h)for(unsigned slot=0;slot<49;++slot){
  d.seek(c);auto s=d.sector(h,slot);assert(s.id_present==(slot<48));
  std::array<u8,20>t;t.fill(255);
  if(slot<48){t[5]=t[17]=0x55;t[6]=c;t[7]=h*48+slot;}
  assert(d.record_format(h,slot,t,true));
  if(slot==48)continue;
  s=d.sector(h,slot);assert(s.data_crc_valid&&s.data[0]==255);
  std::array<u8,256>data;for(unsigned i=0;i<256;++i)data[i]=i^h^c^slot;
  assert(d.record_data(h,slot,data,true));s=d.sector(h,slot);
  assert(s.data==data&&s.cylinder==c&&s.sector==h*48+slot&&s.id_crc_valid);
  assert(d.file[((c*4+h)*48+slot)*256]==data[0]);
  t[7]^=1;assert(!d.record_format(h,slot,t,true));
 }
 d.fail=true;std::array<u8,256>data{};assert(!d.record_data(0,0,data,true));
 assert(!d.sector(0,0).data_present);d.call_unload();assert(!d.sector(0,0).id_present);
 p6066_hdu_device bad;bad.geometry.cylinders=200;assert(bad.call_load().first&&!bad.ready());
 p6066_hdu_device raw;raw.m_chd=false;assert(raw.call_load().first&&!raw.ready());
 puts("PASS: production CHD geometry, sector IDs, addressing, service cylinders, read/write, format and errors");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)/'test.cpp';binary=Path(directory)/'test'
    path.write_text(source)
    subprocess.run(['c++','-std=c++17','-O2',str(path),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
