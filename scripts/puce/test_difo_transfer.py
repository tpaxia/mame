#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""Production DIFO callbacks against synthetic physical sectors and DMA RAM.

Timers and media mechanics are controlled stimuli, not emulated/validated here.
"""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
cpp=(ROOT/'src/devices/bus/p6066/difo.cpp').read_text()
def method(sig):
 a=cpp.index(sig);b=cpp.index('{',a);e=b+1;n=1
 while n:n+=(cpp[e]=='{')-(cpp[e]=='}');e+=1
 return cpp[a:e]
source=r'''
#include "bus/p6066/difo_state.h"
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <algorithm>
template<class... T>void logerror(const char *,T...){}
using u8=uint8_t;using u16=uint16_t;using u32=uint32_t;
#define BIT(v,n) (((v)>>(n))&1)
template<class... T>void fatalerror(const char *s,T...){throw std::runtime_error(s);}
struct attotime {double n;static const attotime never;static attotime from_usec(double v){return {v*1000};}static attotime from_msec(double v){return {v*1000000};}static attotime from_nsec(double v){return {v};}friend attotime operator/(attotime a,int b){return {a.n/b};}};
const attotime attotime::never{-1};
struct emu_timer {bool enabled=false;double delay=-1;void adjust(attotime t){enabled=t.n>=0;delay=t.n;}};
struct p6066_hdu_sector {u8 cylinder=0,sector=0;bool id_present=true,id_crc_valid=true,data_present=true,data_crc_valid=true;std::array<u8,256>data{};};
struct p6066_hdu_device {
 bool present=true,blank=false,bad=false,readonly=false;unsigned cy=0,writes=0,formats=0;
 std::array<p6066_hdu_sector,196> records{};
 std::array<std::array<u8,20>,196> templates{};
 p6066_hdu_device(){for(unsigned h=0;h<4;++h)for(unsigned slot=0;slot<49;++slot){
  auto &s=records[h*49+slot];s.sector=h*48+slot;s.id_present=slot<48;
  for(unsigned i=0;i<256;++i)s.data[i]=i^s.sector^0x5a;}}
 bool ready()const{return present;}unsigned cylinder()const{return cy;}
 void seek(unsigned n){cy=n;}
 p6066_hdu_sector sector(unsigned head,unsigned slot)const {
  auto s=records[head*49+slot];s.cylinder=cy;
  s.id_present&=!blank;s.data_crc_valid&=!bad;return s;
 }
 bool record_data(unsigned h,unsigned slot,const std::array<u8,256> &data,bool valid){
  if(readonly)return false;auto &s=records[h*49+slot];s.data=data;s.data_crc_valid=valid;s.data_present=true;++writes;return true;}
 bool record_format(unsigned h,unsigned slot,const std::array<u8,20> &data,bool){
  if(readonly)return false;templates[h*49+slot]=data;++formats;return true;}
};
struct p6066_difo_device;
struct p6066_bus_device {bool requested=false;void dma_request(p6066_difo_device &,bool v){requested=v;}};
#define TIMER_CALLBACK_MEMBER(n) void n(int param=0)
struct p6066_difo_device {
 p6066_difo_state m_state;std::array<p6066_hdu_device*,2>m_drives;
 p6066_bus_device *m_bus;emu_timer *m_timer;p6066_dma_cycle m_cycle;
 bool m_dma_pending=false,m_dma_granted=false,m_finishing=false,m_dummy=false,m_reset_dma=false;std::array<u8,256>m_buffer{};
 u16 m_byte=0,m_dma_byte=0;u8 m_dma_count=0,m_slot=0,m_revolutions=0,m_phase=0,m_active_unit=0;
 bool m_data_crc=true;
 std::array<u8,20>m_format{};u8 m_preamble=0,m_key_count=0;int m_compare=0;bool m_matched=false;
 emu_timer watchdog;emu_timer *m_watchdog=&watchdog;
 void begin_media();void compare_byte(u8,unsigned);TIMER_CALLBACK_MEMBER(timeout);
 p6066_hdu_device &drive()const{return *m_drives[m_state.unit];}
 void output_data_masked(unsigned,u16,u16);void controller_reset(bool);void execute();void finish(u8 errors=0);void request_data(bool dummy=false);void next_sector();
 TIMER_CALLBACK_MEMBER(tick);
 p6066_dma_cycle dma_grant();void dma_done(u16,bool);
};
'''
for sig in ['void p6066_difo_device::output_data_masked(', 'void p6066_difo_device::controller_reset(', 'void p6066_difo_device::execute()', 'void p6066_difo_device::begin_media()', 'void p6066_difo_device::compare_byte(', 'TIMER_CALLBACK_MEMBER(p6066_difo_device::timeout)', 'void p6066_difo_device::finish(', 'void p6066_difo_device::next_sector()', 'void p6066_difo_device::request_data(', 'TIMER_CALLBACK_MEMBER(p6066_difo_device::tick)', 'p6066_dma_cycle p6066_difo_device::dma_grant()', 'void p6066_difo_device::dma_done(']:
 body=method(sig).replace('TIMER_CALLBACK_MEMBER(p6066_difo_device::tick)','void p6066_difo_device::tick(int param)')
 body=body.replace('TIMER_CALLBACK_MEMBER(p6066_difo_device::timeout)','void p6066_difo_device::timeout(int param)')
 source+=body+'\n'
source+=r'''
int main()
{
 for(unsigned odd=0;odd<2;++odd)for(unsigned head=0;head<4;++head)for(unsigned unit=0;unit<2;++unit)
 {
  p6066_difo_device d;p6066_hdu_device disk[2];p6066_bus_device bus;emu_timer timer;
  d.m_drives={&disk[0],&disk[1]};d.m_bus=&bus;d.m_timer=&timer;
  std::array<u16,65536>ram;ram.fill(0xcccc);
  unsigned pointer=0xfffe + odd; // exercises DMA byte address above 64 KiB
  d.m_state.unit=unit;d.m_state.word=pointer>>1;d.m_state.odd=odd;
  d.m_state.sector=head*48;d.m_state.length=0;d.m_state.execute=4;
  d.execute();unsigned requests=0;
  for(unsigned n=0;n<1000&&!d.m_state.completion;++n)
  {
   d.tick();if(bus.requested)
   {
    auto cycle=d.dma_grant();++requests;assert(!bus.requested&&cycle.write);
    ram[cycle.address]=(ram[cycle.address]&~cycle.mask)|(cycle.data&cycle.mask);
    d.dma_done(0,false);
   }
  }
  assert(d.m_state.completion&&d.m_state.status==0&&requests==128+odd);
  auto byte=[&](unsigned a){return (ram[a/2]>>(a&1?0:8))&255;};
  for(unsigned i=0;i<256;++i)assert(byte(pointer+i)==(i^(head*48)^0x5a));
  assert(byte(pointer-1)==0xcc&&byte(pointer+256)==0xcc);
  assert(d.m_state.byte_address()==pointer+256&&d.m_state.sector==head*48+1);
 }
 for(unsigned bad=0;bad<2;++bad)
 {
  p6066_difo_device d;p6066_hdu_device disk[2];p6066_bus_device bus;emu_timer timer;
  d.m_drives={&disk[0],&disk[1]};d.m_bus=&bus;d.m_timer=&timer;disk[0].bad=bad;
  d.m_state.length=0;d.m_state.execute=8;d.execute();
  for(unsigned n=0;n<1000&&!d.m_state.completion;++n){d.tick();assert(!bus.requested);}
  assert(d.m_state.completion&&d.m_state.status==(bad?0x40:0));
 }
 {
  p6066_difo_device d;p6066_hdu_device disk[2];p6066_bus_device bus;emu_timer timer;
  d.m_drives={&disk[0],&disk[1]};d.m_bus=&bus;d.m_timer=&timer;disk[0].blank=true;
  d.m_state.length=0;d.m_state.execute=4;d.execute();
  for(unsigned n=0;n<1000&&!d.m_state.completion;++n){d.tick();assert(!bus.requested);}
  assert(!d.m_state.completion&&d.m_watchdog->enabled);d.timeout();assert(d.m_state.status==0x80);
 }

 // Writes must retain IDs, read RAM without byte-write masks, include four
 // non-advancing preamble requests, and preserve byte order across 64 KiB.
 for(unsigned odd=0;odd<2;++odd)for(unsigned head=0;head<4;++head) {
  p6066_difo_device d;p6066_hdu_device disk[2];p6066_bus_device bus;emu_timer timer;
  d.m_drives={&disk[0],&disk[1]};d.m_bus=&bus;d.m_timer=&timer;
  unsigned base=0xfffe + odd;d.m_state.word=base/2;d.m_state.odd=odd;
  d.m_state.sector=head*48;d.m_state.length=1;d.m_state.execute=16;d.execute();unsigned count=0,dummies=0;
  auto byte=[&](unsigned a){return u8((a-base)*7+3);};
  for(unsigned n=0;n<2000&&!d.m_state.completion;++n){d.tick();if(bus.requested){
   auto c=d.dma_grant();assert(!c.write&&c.mask==0xffff);++count;
   if(d.m_dummy){assert(c.address==(base+(disk[0].writes?256:0))/2);++dummies;}
   d.dma_done((u16(byte(c.address*2))<<8)|byte(c.address*2+1),false);
  }}
  assert(d.m_state.status==0&&disk[0].writes==2&&count==2*(132+odd)&&dummies==8);
  for(unsigned sector=0;sector<2;++sector)for(unsigned i=0;i<256;++i)
   assert(disk[0].records[head*49+sector].data[i]==byte(base+sector*256+i));
  assert(d.m_state.byte_address()==base+512&&d.m_state.sector==head*48+2);
 }
 // Format ignores length and transfers exactly 49 templates. Seven dummy
 // word reads per slot retain the pointer. Completion follows closing index.
 for(unsigned h=0;h<4;++h){
  p6066_difo_device d;p6066_hdu_device disk[2];p6066_bus_device bus;emu_timer timer;
  d.m_drives={&disk[0],&disk[1]};d.m_bus=&bus;d.m_timer=&timer;
  d.m_state.execute=128;d.m_state.sector=h*48;d.m_state.word=0x8000;d.execute();
  unsigned requests=0,dummies=0;
  for(unsigned n=0;n<3000&&!d.m_state.completion;++n){d.tick();if(bus.requested){
   auto c=d.dma_grant();++requests;if(d.m_dummy){++dummies;assert(c.address==0x8000+disk[0].formats*10);}
   d.dma_done(c.address,false);
  }}
  assert(d.m_state.completion&&d.m_state.status==0&&disk[0].formats==49);
  assert(requests==49*17&&dummies==49*7&&d.m_state.word==0x8000+490);
  for(unsigned slot=0;slot<49;++slot)for(unsigned word=0;word<10;++word){
   auto &t=disk[0].templates[h*49+slot];unsigned v=0x8000+slot*10+word;
   assert(t[word*2]==(v>>8)&&t[word*2+1]==(v&255));
  }
 }
 // Scan: first unequal key, second qualifying key; NUC is the zero-based
 // index, SAT stays matched, pointer rewinds, and comparison direction is RAM
 // versus disk. Test equality, <=, >= and FF wildcards in both RAM alignments.
 for(unsigned odd=0;odd<2;++odd)for(unsigned mode=0;mode<3;++mode){
  p6066_difo_device d;p6066_hdu_device disk[2];p6066_bus_device bus;emu_timer timer;
  d.m_drives={&disk[0],&disk[1]};d.m_bus=&bus;d.m_timer=&timer;
  auto &data=disk[0].records[0].data;data.fill(0x30);data[0]=mode==2?0x20:0x40;
  d.m_state.execute=32;d.m_state.length=0;d.m_state.key=0xfe;d.m_state.scan=(mode*16)|1;
  unsigned base=0x10000+odd;d.m_state.word=base/2;d.m_state.odd=odd;d.execute();
  auto templ=[&](unsigned a){return u8(((a-base)&1)?0xff:mode==1?0x31:mode==2?0x2f:0x30);};
  unsigned requests=0;
  for(unsigned n=0;n<1000&&!d.m_state.completion;++n){d.tick();if(bus.requested){auto c=d.dma_grant();++requests;
   d.dma_done((u16(templ(c.address*2))<<8)|templ(c.address*2+1),false);}}
  assert(d.m_state.completion&&d.m_state.keys==1&&d.m_state.sector==0);
  assert(d.m_state.status==(mode?12:8)&&requests==128+odd&&d.m_state.byte_address()==base);
 }
 // Longer seek and home use nonzero bounded timing, busy until movement ends,
 // and no invented class-C completion.
 {
  p6066_difo_device d;p6066_hdu_device disk[2];p6066_bus_device bus;emu_timer timer;
  d.m_drives={&disk[0],&disk[1]};d.m_bus=&bus;d.m_timer=&timer;
  d.m_state.cylinder=201;d.execute();assert(d.m_state.busy&&timer.delay==164000000);
  d.tick();assert(disk[0].cy==201&&!d.m_state.busy&&!d.m_state.completion);
  d.m_state.home=true;d.execute();d.tick();assert(disk[0].cy==0&&!d.m_state.completion);
 }
 // An invalid dummy read is a real fault. A timeout during granted DMA must
 // not publish completion until that owned cycle returns.
 for(unsigned fault=0;fault<2;++fault){
  p6066_difo_device d;p6066_hdu_device disk[2];p6066_bus_device bus;emu_timer timer;
  d.m_drives={&disk[0],&disk[1]};d.m_bus=&bus;d.m_timer=&timer;
  d.m_state.execute=16;d.m_state.length=0;d.execute();d.tick();d.tick();assert(bus.requested&&d.m_dummy);
  d.dma_grant();if(fault){d.timeout();assert(!d.m_state.completion&&d.m_finishing);}
  d.dma_done(0,!fault);assert(d.m_state.completion&&d.m_state.status==(fault?0x80:0x22));
 }



 // ECOC qualifies register loading. All plain ECD values/masks must leave
 // setup latches untouched; ECOT advances only an owned completion response.
 for(unsigned value=0;value<65536;++value)for(unsigned mask:{0xffU,0xff00U,0xffffU})
 for(unsigned response=0;response<4;++response){
  p6066_difo_device d;d.m_state.execute=0x10;d.m_state.word=0xa55a;
  d.m_state.cylinder=123;d.m_state.sector=67;d.m_state.length=89;
  d.m_state.servicing=response<3;d.m_state.response=response<3?response:0;
  d.output_data_masked(response<3?3:4,value,mask);
  assert(d.m_state.execute==0x10&&d.m_state.word==0xa55a&&d.m_state.cylinder==123);
  assert(d.m_state.sector==67&&d.m_state.length==89);
  assert(d.m_state.response==(response<3?std::min(response+1,2U):0));
 }
 // Exhaust raw key seeds, comparison directions and first-key carry edges.
 // Expected terminal index is defined by key length, independently of the
 // production increment-to-carry implementation.
 for(unsigned length=1;length<=256;++length)for(unsigned mode=0;mode<3;++mode)
 for(int relation=-1;relation<=1;++relation){
  p6066_difo_device d;d.m_buffer.fill(0x80);d.m_state.key=u8(256-length);
  d.m_key_count=d.m_state.key;d.m_state.scan=mode*16;
  for(unsigned i=0;i<length;++i){
   const u8 value=i==0?u8(0x80+relation):0xff;
   d.compare_byte(value,i);
   if(i+1<length)assert(d.m_state.keys==0xff&&!d.m_matched);
  }
  bool matched=relation==0||(mode==1&&relation>0)||(mode==2&&relation<0);
  assert(d.m_state.keys==0&&d.m_matched==matched);
  assert(d.m_state.status==(matched?(relation?12:8):0));
 }
 // P6066 firmware sends N-1; terminal zero is sampled before decrement. PAS is raw.
 // No hidden P+1 conversion or invented cylinder carry occurs here.
 for(unsigned step=0;step<16;++step){
  p6066_difo_device d;emu_timer timer;d.m_timer=&timer;
  d.m_state.execute=32;d.m_state.scan=step;d.m_state.word=0x8080;
  d.next_sector();assert(d.m_state.length==255&&d.m_state.sector==step&&d.m_state.word==0x8000&&d.m_state.completion);
 }
 // Exhaustive raw counter seeds, independent N-1 expectation across all
 // sector-counted operations. FORMAT has a fixed physical-slot count instead.
 for(unsigned op:{4U,8U,16U,32U})for(unsigned count=1;count<=256;++count){
  p6066_difo_device d;emu_timer timer;d.m_timer=&timer;
  d.m_state.execute=op;d.m_state.length=count-1;
  for(unsigned done=1;done<=count;++done){
   d.next_sector();assert(d.m_state.completion==(done==count));
   assert(d.m_state.length==u8(count-1-done));
  }
 }
 // Reset before and after DMA grant: withdraw unowned request, drain owned
 // cycle once, and never generate a stale completion after ECOR.
 for(unsigned granted=0;granted<2;++granted){
  p6066_difo_device d;p6066_hdu_device disk[2];p6066_bus_device bus;emu_timer timer;
  d.m_drives={&disk[0],&disk[1]};d.m_bus=&bus;d.m_timer=&timer;
  d.m_state.execute=4;d.m_state.length=0;d.execute();d.tick();d.tick();assert(bus.requested);
  if(granted)d.dma_grant();d.controller_reset(true);assert(!bus.requested&&!d.m_state.completion);
  if(granted){assert(d.m_state.busy);d.dma_done(0,true);}
  assert(!d.m_dma_pending&&!d.m_state.busy&&!d.m_state.completion&&!d.m_watchdog->enabled);
 }
 // Late RAM response and host write protection must not look like a success.
 for(unsigned late=0;late<2;++late){
  p6066_difo_device d;p6066_hdu_device disk[2];p6066_bus_device bus;emu_timer timer;
  d.m_drives={&disk[0],&disk[1]};d.m_bus=&bus;d.m_timer=&timer;disk[0].readonly=!late;
  d.m_state.execute=16;d.m_state.length=0;d.execute();
  for(unsigned n=0;n<500&&!d.m_state.completion;++n){d.tick();if(bus.requested){
   d.dma_grant();if(late&&!d.m_dummy){d.tick();assert(d.m_finishing&&!d.m_state.completion);}
   d.dma_done(0x1234,false);
  }}
  assert(d.m_state.completion&&d.m_state.status==(late?0x40:0x20));
  if(late)assert(disk[0].writes==1&&!disk[0].records[0].data_crc_valid);
 }
 puts("PASS: production DIFO read/verify, 4 heads x 2 units, odd/even DMA above 64KiB, write/format/scan/seek/home, preamble DMA, CRC and timeout faults");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'test.cpp';p.write_text(source);binary=Path(tmp)/'test'
 subprocess.run(['c++','-std=c++17','-O2','-I',str(ROOT/'src/devices'),str(p),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
