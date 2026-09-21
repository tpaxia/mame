#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""RODMA arbitration and production PUCE memory-wait continuation tests.

Synthetic RAM completion is explicitly supplied by the test. No ROM/guest boot.
"""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
cpp = (ROOT/'src/devices/cpu/puce/puce.cpp').read_text()
def method(signature):
    a=cpp.index(signature); b=cpp.index('{',a); e=b+1; depth=1
    while depth:
        depth+=(cpp[e]=='{')-(cpp[e]=='}'); e+=1
    return cpp[a:e].replace('puce_device','fixture')
source=r'''
#include "bus/p6066/dma.h"
#include "cpu/puce/puce_state.h"
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <string>
using u8=uint8_t;using u16=uint16_t;using u32=uint32_t;
namespace util {template<class... T>std::string string_format(const char *s,T...){return s;}}
template<class... T>void fatalerror(const char *s,T...){throw std::runtime_error(s);}
template<class... T>void osd_printf_info(const char *,T...){}
struct input_port {
 unsigned value=0;bool isunset(){return false;}
 unsigned operator()(unsigned=0){return value;}
};
struct fixture {
 template<class... T>void logerror(const char *,T...){}
 puce_state m_core;
 bool m_stopped=false,m_invalid_pending=false,m_hold_on_unsupported=false;
 unsigned m_phase=0,m_invalid_cycles=0,m_fetch_pc=0,m_ir=0;
 int m_icount=0;
 bool m_memory_active=false,ready=false,all_shared=false;
 u8 m_sampled=0,m_sample_input=0;u16 m_sample_type=0;u32 m_fetch_invalid_before=0;
 unsigned begins=0,commits=0,inputs=0,types=0,strobes=0,outputs=0,alfa=0,beta=0;
 unsigned request_address=0;u16 request_data=0,request_mask=0,result=0;
 std::array<u16,65536> ram{};
 struct memory {
  fixture &f;
  u16 read_word(u16 a,u16=65535){return f.ram[a];}
  void write_word(u16 a,u16 v,u16 mask=65535){f.ram[a]=(f.ram[a]&~mask)|(v&mask);}
 }m_program{*this};
 struct memory_wait{};struct channel_adapter;
 void execute_run();u16 memory_read(u16,u16=65535);void memory_write(u16,u16,u16=65535);
 bool m_shared_memory_cb(unsigned a){return all_shared || a<0x8000;}
 void m_memory_begin_cb(unsigned a,u16 v,u16 mask){++begins;request_address=a;request_data=v;request_mask=mask;ready=false;}
 bool m_memory_ready_cb(){return ready;}
 u16 m_memory_data_cb(){return result;}
 void m_phase_cb(unsigned b){b ? ++beta : ++alfa;}
 void debugger_instruction_hook(unsigned){}void update_ecorn(){}void m_stopped_cb(unsigned){}
 void m_interrupt_sync_cb(unsigned){}unsigned m_irq_request_cb(unsigned){return 0;}
 void m_irq_ack_cb(unsigned){}void m_irq_end_cb(unsigned){}
 unsigned m_input_data_cb(unsigned){++inputs;return 0xa5;}
 unsigned m_name_type_cb(unsigned){++types;return 0xb600;}
 void m_data_cb(unsigned,u16,u16){++outputs;}
 void m_command_cb(unsigned,u16,u16){++outputs;}void m_select_cb(unsigned){++outputs;}
 void m_strobe_cb(unsigned){++strobes;}void m_control_cb(unsigned,unsigned){}
 void m_service_console_cb(unsigned){}void m_service_console_control_cb(unsigned){}
 input_port m_service_console_input_cb,m_ecof_cb;
 fixture(){m_core.reset();m_core.cpu19m=true;ram.fill(0x1234);}
 void tick(){m_icount=1;execute_run();}
 void complete(){assert(!ready);++commits;if(request_address&0x10000)m_program.write_word(request_address,request_data,request_mask);else result=m_program.read_word(request_address,request_mask);ready=true;}
 void instruction(u16 op){ram[0x8000]=op;tick();assert(m_phase==1);tick();assert(m_memory_active);}
};
'''
source+=method('struct puce_device::channel_adapter')+';\n'
for name in ['u16 puce_device::memory_read','void puce_device::memory_write','void puce_device::execute_run']:
    source+=method(name)+'\n'
source+=r'''
int main()
{
 p6066_dma_arbiter a;
 for(unsigned mode=1;mode<=3;++mode)for(unsigned boundary=0x800;boundary<=0xf800;boundary+=0x800)
 {
  a.mode=mode;a.boundary=boundary;
  for(unsigned addr=0;addr<65536;++addr)
   assert(a.shared(addr)==(mode==1 ? addr<boundary : mode==2 ? addr>=boundary : addr>=boundary&&addr<0x8000));
 }
 for(unsigned first=0;first<16;++first)for(unsigned second=first;second<16;++second)
 {
  a.reset();a.requests[first]=a.requests[second]=true;a.cpu_pending=true;
  a.synchronize();assert(a.choose()==int(first));assert(a.choose()==-1);
  a.requests[first]=false;a.complete();
  if(second!=first){assert(a.choose()==int(second));a.requests[second]=false;a.complete();}
  assert(a.choose()==16);a.complete();assert(a.choose()==-1);
 }
 a.reset();a.requests[3]=true;assert(a.choose()==-1); // not eligible before sync
 a.synchronize();assert(a.choose()==3);a.requests[3]=false;a.requests[7]=true;
 a.complete();assert(a.choose()==7); // queue sampling, no ALFA needed
 // ALFA fetch continuation: no re-arbitration, no second PC increment.
 {
  fixture f;f.all_shared=true;f.ram[0x8000]=0xc900;f.tick();
  assert(f.m_phase==2&&f.m_core.pc()==0x8000&&f.begins==1&&f.alfa==1);
  for(int i=0;i<10;++i)f.tick();assert(f.alfa==1&&f.begins==1);
  f.complete();f.tick();assert(f.m_phase==1&&f.m_core.pc()==0x8001);
  f.tick();assert(f.m_phase==0&&f.commits==1);
 }
 // Indexed aliased store: updated L2 must be written exactly once.
 {
  fixture f;f.m_core.l[2]=0x100;f.instruction(0xee22);
  assert(f.request_address==0x10100&&f.request_data==0x101&&f.m_core.l[2]==0x100);
  for(int i=0;i<9;++i)f.tick();assert(f.begins==1&&f.ram[0x100]==0x1234);
  f.complete();f.tick();assert(f.ram[0x100]==0x101&&f.m_core.l[2]==0x101&&f.commits==1);
 }
 // Indexed aliased load overwrites index, not loaded value plus one.
 {
  fixture f;f.m_core.l[2]=0x100;f.ram[0x100]=0x6789;f.instruction(0xde22);
  f.complete();f.tick();assert(f.m_core.l[2]==0x6789&&f.begins==1);
 }
 // Odd byte store preserves high byte and pre-index address.
 {
  fixture f;f.m_core.l[2]=0x201;f.m_core.l[3]=0x7e;f.instruction(0x8823);
  assert(f.request_mask==0xff&&f.request_data==0x7e&&f.request_address==0x10100);
  f.complete();f.tick();assert(f.ram[0x100]==0x127e&&f.m_core.l[2]==0x202);
 }
 // SEIP word: sample input/type before wait, one post-write ECOT.
 {
  fixture f;f.m_core.l[2]=0x100;f.instruction(0xeb28);
  assert(f.inputs==1&&f.types==1&&f.strobes==0&&f.request_data==0xb6a5);
  for(int i=0;i<9;++i)f.tick();assert(f.inputs==1&&f.strobes==0);
  f.complete();f.tick();assert(f.inputs==1&&f.types==1&&f.strobes==1&&f.ram[0x100]==0xb6a5);
  assert(f.m_core.l[2]==0x101);
 }
 // Memory-to-channel output cannot precede DMA memory completion.
 {
  fixture f;f.m_core.l[2]=0x100;f.instruction(0xf728); // canonical low nibble 0
 }
 puts("PASS: DMA partitions/priority/queues and production CPU wait continuations");
}
'''
# Correct canonical ESIP encoding: x=2, low nibble0.
source=source.replace('f.instruction(0xf728); // canonical low nibble 0', 'f.instruction(0xf720);assert(f.outputs==0);f.complete();f.tick();assert(f.outputs==1&&f.m_core.l[2]==0x101);')
with tempfile.TemporaryDirectory(prefix='p6066-dma-') as tmp:
    p=Path(tmp)/'test.cpp';p.write_text(source);binary=Path(tmp)/'test'
    subprocess.run(['c++','-std=c++17','-O2','-I',str(ROOT/'src/devices'),str(p),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
