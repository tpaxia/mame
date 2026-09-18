#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""CPU19 V2 pp.10,14: branches and INCD, including ALFA counter aliases."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TEST = r'''
#include "cpu/puce/puce_state.h"
#include <cassert>
#include <iostream>
int main() {
 unsigned long count=0;
 for(unsigned level=1;level<=4;++level)
 for(unsigned base:{0x8000U,0xc000U})
 for(unsigned before:{0x0000U,0x00feU,0x00ffU,0x1fffU,0x7fffU,0x9fffU,0xffffU}) {
  const unsigned counter=level==1?12:level==2?13:level==3?1:0;
  puce_state initial;initial.level=level;initial.interrupt_base=base;
  initial.l.fill(0x5aa5);initial.set_pc(before);
  // Independent ALFA expectation: short counters preserve their high byte.
  auto advanced=initial.l;
  if(level<3) advanced[counter]=(advanced[counter]&0xff00)|((before+1)&255);
  else advanced[counter]=(before+1)&65535;
  for(unsigned target=0;target<8192;++target) {
   auto c=initial;c.di=target&255;auto expected=advanced;
   expected[counter]=level<3?(expected[counter]&0xff00)|(target&255)
    :(expected[counter]&0xe000)|target;
   c.advance();assert(c.execute_register(target));
   assert(c.l==expected && c.di==(target&255) && c.level==level);++count;
  }
  for(unsigned condition=0;condition<16;++condition)
  for(unsigned flags=0;flags<256;++flags)
  for(unsigned target:{0U,1U,127U,128U,254U,255U}) {
   auto c=initial;c.di=flags;auto expected=advanced;
   bool bit=flags&(1U<<(condition/2));
   if(bit==bool(condition%2)) expected[counter]=(expected[counter]&0xff00)|target;
   c.advance();assert(c.execute_register(0x6000|(condition<<8)|target));
   assert(c.l==expected && c.di==flags && c.level==level);++count;
  }
  // All DI patterns, selectors and carry boundaries; counter destinations
  // see the post-fetch value. INCD increments full L even for selectors 12–15.
  for(unsigned condition=0;condition<16;++condition)
  for(unsigned flags=0;flags<256;++flags)
  for(unsigned dest=0;dest<16;++dest)
  for(unsigned value:{0U,0xffU,0x7fffU,0xffffU}) {
   auto c=initial;if(dest!=counter)c.l[dest]=value;c.di=flags;
   auto expected=c.l;expected[counter]=advanced[counter];
   if(bool(flags&(1U<<(condition/2)))==bool(condition%2))
    expected[dest]=(expected[dest]+1)&65535;
   c.advance();assert(c.execute_register(0xa000|(condition<<4)|dest));
   assert(c.l==expected && c.di==flags && c.level==level);++count;
  }
 }
 std::cout<<"PASS: "<<count<<" SAI/SAD/INCD cases, all levels and post-fetch aliases\n";
}
'''
if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='puce-branches-') as temp:
        source = Path(temp)/'test.cpp'
        binary = Path(temp)/'test'
        source.write_text(TEST)
        subprocess.run(['c++', '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I', str(ROOT/'src/devices'), str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
