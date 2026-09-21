#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""Exercise production RODMA timer callbacks with controlled bus stimuli."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
cpp = (ROOT / 'src/devices/bus/p6066/rodma.cpp').read_text()
source = r'''
#include "bus/p6066/dma.h"
#include <cassert>
#include <cstdio>
#include <stdexcept>
using u8=uint8_t;using u16=uint16_t;using offs_t=uint32_t;
#define BIT(v,n) (((v)>>(n))&1)
void fatalerror(const char *) { throw std::runtime_error("invalid DMA"); }
struct attotime { int ns; static const attotime zero,never;
 static attotime from_nsec(int n){return {n};} };
const attotime attotime::zero{0},attotime::never{-1};
struct emu_timer {int delay=-1;void adjust(attotime t){delay=t.ns;}};
struct bus {
 p6066_dma_arbiter *arb;
 unsigned grants=0,done=0,cycles=0,traps=0;
 int last=-1;bool invalid=false;u16 value=0x1234;
 p6066_dma_cycle dma_grant(int owner){++grants;last=owner;arb->requests[owner]=false;return {0x8001,0,0xffff,false};}
 u16 dma_memory_cycle(p6066_dma_cycle &,bool &bad){++cycles;bad=invalid;return value;}
 void dma_done(int owner,u16 data,bool bad){++done;assert(owner==last&&data==value&&bad==invalid);}
 void dma_cpu_invalid(){++traps;}
};
struct p6066_rodma_device {
 p6066_dma_arbiter m_dma;p6066_dma_cycle m_cpu_cycle,m_cycle;
 emu_timer timer;emu_timer *m_timer=&timer;bus b;bus *m_bus=&b;
 bool m_both_phases=false,m_cpu_ready=false,m_invalid=false;
 u16 m_result=0,m_cpu_result=0;u8 m_stage=0;
 p6066_rodma_device(){b.arb=&m_dma;}
 void reset_transport();void phase(unsigned);void request(unsigned,bool);
 void cpu_begin(offs_t,u16,u16);void dispatch();void tick(int=0);
};
'''
for signature in ['void p6066_rodma_device::reset_transport()',
                  'void p6066_rodma_device::phase(',
                  'void p6066_rodma_device::request(',
                  'void p6066_rodma_device::cpu_begin(',
                  'void p6066_rodma_device::dispatch()',
                  'TIMER_CALLBACK_MEMBER(p6066_rodma_device::tick)']:
    start = cpp.index(signature)
    end = cpp.index('{', start) + 1
    depth = 1
    while depth:
        depth += (cpp[end] == '{') - (cpp[end] == '}')
        end += 1
    source += cpp[start:end].replace('TIMER_CALLBACK_MEMBER(p6066_rodma_device::tick)',
                                    'void p6066_rodma_device::tick(int param)') + '\n'
source += r'''
int main(){
 p6066_rodma_device d;
 d.request(3,true);assert(d.m_stage==0);
 d.phase(1);assert(d.m_stage==0); // RODMA samples ALFA only
 d.phase(0);assert(d.b.last==3&&d.timer.delay==150);
 d.cpu_begin(0x10042,0xabcd,0xff00); // pending CPU write
 d.request(1,true); // queued peripheral wins at DONE
 d.tick();assert(d.timer.delay==385&&d.b.cycles==0);
 d.tick();assert(d.b.done==1&&d.b.cycles==1&&d.timer.delay==200);
 d.tick();assert(d.b.last==1&&d.m_dma.owner==1);
 d.b.invalid=true;
 d.tick();d.tick();assert(d.b.done==2&&d.b.traps==0);
 d.tick();assert(d.m_dma.owner==16&&d.timer.delay==0);
 assert(d.m_cycle.address==0x42&&d.m_cycle.write&&d.m_cycle.data==0xabcd&&d.m_cycle.mask==0xff00);
 d.tick();d.tick();assert(!d.m_cpu_ready&&d.timer.delay==75);
 d.tick();assert(d.m_cpu_ready&&d.b.traps==1&&d.m_cpu_result==0x1234);
 d.b.value=0x5678;d.b.invalid=false;d.request(0,true);d.phase(0);
 d.tick();d.tick();d.tick();assert(d.m_cpu_result==0x1234);
 d.reset_transport();assert(d.m_stage==0&&d.timer.delay==-1&&!d.m_cpu_ready);
 d.m_both_phases=true;d.request(2,true);d.phase(1);assert(d.m_dma.owner==2);
 puts("PASS: production RODMA grants, timer ordering, queued priority, CPU result retention and owner-qualified invalid memory");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'test.cpp'
    binary = Path(directory) / 'test'
    path.write_text(source)
    subprocess.run(['c++', '-std=c++17', '-O2', '-I', str(ROOT / 'src/devices'),
                    str(path), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
