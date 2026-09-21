#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""Independent DIFO register, address and three-byte response expectations."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
source=r'''
#include "bus/p6066/difo_state.h"
#include <cassert>
#include <cstdio>
int main()
{
 p6066_difo_state d;
 for(unsigned v=0;v<256;++v)
 {
  d.select(v);assert(d.selected==((v&0x78)==0x50));
  if(d.selected)assert(d.unit==(v>>7));
 }
 for(unsigned a=0;a<131072;++a)
 {
  d.latch(0x800|(a&255));d.latch(0xa00|(a>>8));
  assert(d.word==(a>>1)&&d.odd==bool(a&1)&&d.byte_address()==a);
 }
 d.latch(0x035);d.latch(0x2c9);d.latch(0x4bf);d.latch(0x67f);d.latch(0xdfe);
 assert(d.scan==0x35&&d.cylinder==201&&d.sector==191&&d.head()==3&&d.length==127);
 assert(d.key==254&&d.home);
 for(unsigned v=0;v<256;++v)
 {
  assert(d.latch(0xe00|v));
  unsigned op=v&0xbc;
  assert(d.operation_bits()==op);
  assert(d.valid_operation()==(op==0||op==4||op==8||op==16||op==32||op==128));
 }
 // All eight physical selector rows, both ECD8 values, every payload.
 // Separate expected latch fields also check that unrelated latches survive.
 for(unsigned row=0;row<8;++row)for(unsigned payload=0;payload<512;++payload)
 {
  p6066_difo_state x;
  x.scan=0x35;x.cylinder=0x71;x.sector=0x56;x.length=0x37;
  x.word=0xabcd;x.odd=true;x.key=0x91;x.home=true;x.execute=0x24;
  const bool execute=x.latch((row<<9)|payload);
  assert(execute==(row==7));
  assert(x.scan==(row==0?(payload&255):0x35));
  assert(x.cylinder==(row==1?(payload&255):0x71));
  assert(x.sector==(row==2?(payload&255):0x56));
  assert(x.length==(row==3?(payload&255):0x37));
  const unsigned address=row==4?((0xabcd&0xff80)|(payload>>1&127)):
                         row==5?((payload<<7)|(0xabcd&127)):0xabcd;
  assert(x.word==address);
  assert(x.odd==(row==4?bool(payload&1):true));
  assert(x.key==(row==6?(payload&255):0x91));
  assert(x.home==(row==6?bool(payload&256):true));
  assert(x.execute==(row==7?(payload&255):0x24));
 }
 d.reset();assert(d.type(false)==0xb200&&d.type(true)==0xb650);
 assert(d.selection_status(true)==0&&d.selection_status(false)==0x20);
 d.busy=true;assert(d.selection_status(true)==6&&d.selection_status(false)==0x26);
 d.keys=17;d.sector=48;d.finish(0x62);
 assert(!d.busy&&!d.request);d.synchronize();assert(d.request);
 d.acknowledge();assert(d.input(true)==0x62);
 for(int i=0;i<10;++i)assert(d.input(true)==0x62); // reads have no side effects
 d.strobe();assert(d.input(true)==17);d.synchronize();assert(d.request);
 d.strobe();assert(d.input(true)==48&&d.request);
 d.synchronize();assert(!d.request&&!d.servicing&&!d.completion);
 d.name=9;d.reset();assert(d.name==9&&!d.selected&&!d.busy&&!d.request);
 puts("PASS: DIFO all select bytes, 131072 pointer encodings, execute masks, status and ECOT/ECM3 response phases");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'test.cpp';p.write_text(source);binary=Path(tmp)/'test'
 subprocess.run(['c++','-std=c++17','-O2','-I',str(ROOT/'src/devices'),str(p),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
