// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
// Standalone: c++ -std=c++17 scripts/puce/test_flodi_latches.cpp -o /tmp/flodi-latches
#include "../../src/devices/bus/p6066/flodi_latches.h"
#include <cassert>
#include <cstdio>
int main()
{
 using effect = p6066_flodi_latches::effect;
 p6066_flodi_latches f;
 // Table 5 HOME direction/start bytes: CLAB (ECD6) excludes local mode.
 assert(f.write(false,0x40)==effect::command && f.command==0x40);
 assert(f.write(false,0x50)==effect::command && f.command==0x50 && f.busy());
 f.ecm3();
 // Table 5: both selection commands reach the register, even with PRICO set.
 assert(f.write(false,0x42)==effect::command && !f.busy());
 assert(f.write(false,0x52)==effect::command && f.command==0x52 && f.busy());
 f.ecm3();
 assert(f.status(false,false,false,0)==0x06);
 assert(f.status(false,false,true,0)==0x46);
 // Stop positioning and count time; CATE remains set after INCO clears COTE.
 assert(f.write(true,0xc0)==effect::command && f.cote);
 f.ecm3();
 assert(f.status(false,false,true,0)==0x42);
 assert(f.write(true,0x80)==effect::command);
 assert(f.write(true,0xfa)==effect::inco && f.command==0x80 && f.mas==0xfa && !f.cote);
 f.ecm3();
 assert(f.status(false,true,true,0)==0x41);
 // Table 4 / K07 PIFU: PIZE during function, ERRO during completion.
 assert(f.status(false,false,true,0x80)==0xc0);
 assert(f.status(true,false,true,0x80)==0x80);
 assert(f.status(true,false,false,0xc0)==0xc0);
 // p.37: first end command switches to NUM *before* ECM3.
 f.num=0x37;
 assert(f.write(true,0)==effect::command && f.command==0 && f.cote);
 assert(f.status(true,false,true,0xc0)==0x37);
 assert(f.write(true,0xa5)==effect::inco && f.command==0 && f.mas==0xa5 && f.cote);
 f.ecm3();
 assert(!f.prico && !f.busy());
 // Local commands leave the control register untouched (COLON).
 assert(f.write(false,0)==effect::local && f.command==0);
 assert(f.write(false,0)==effect::local && f.command==0);
 puts("PASS: FLODI selection/function command gating, CATE/COTE, PRICO/NUM and PIFU");
}
