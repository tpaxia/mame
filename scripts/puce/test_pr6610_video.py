#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""ROM-free combined GO011/GOINO bus routing test, using production bus/GO011 methods."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
# Reuse the GO011 production-body extraction, not its standalone main/test.
go011_test = ROOT / "scripts/puce/test_go011.py"
script = go011_test.read_text()
prefix = script[:script.index("source += r'''\nstruct fixture")]
namespace = {"__file__": str(go011_test)}
exec(compile(prefix, str(go011_test), "exec"), namespace)
source = namespace["source"]
source = source.replace(
    '#include "bus/p6066/go011_state.h"',
    '#include "bus/p6066/go011_state.h"\n#include "bus/p6066/goino_state.h"\n#include <tuple>\n#include <vector>',
    1,
)
source += r"""
#define BIT(v, n) (((v) >> (n)) & 1)
struct bitmap_stub {
 int m_xpos=28,m_ypos=24;
 std::vector<std::tuple<int,int,unsigned>> pixels;
 void draw_pixel(int x,int y,unsigned color){pixels.emplace_back(x,y,color);}
 void check_new_page(){}
};
struct p6066_pr6610_device {
 static constexpr int LEFT_MARGIN=28,COLUMN_PITCH=3,PAPER_WIDTH=1736,FEED_HALF=7;
 bitmap_stub bitmap,*m_bitmap=&bitmap;
 unsigned m_columns_cached=0,m_feed_events_cached=0,m_feed_rows_total=0,m_feed_half=0;
 unsigned m_columns_rendered=0,m_feed_rows=0,m_head_x=0,m_head_y=0;
 bool m_resync=true;
 void render_column(u8);
 void advance_feed();
 void tick(p6066_goino_state &);
};
"""
for signature in (
    "void p6066_pr6610_device::render_column(",
    "void p6066_pr6610_device::advance_feed(",
    "void p6066_pr6610_device::tick(",
):
    source += namespace["method"]("src/devices/bus/p6066/pr6610.cpp", signature)
source += r"""
struct console_card : device_p6066_card_interface {
 p6066_goino_state state;
 console_card(){state.printer_attached=true;}
 void select(u8 name) override {state.select(name);}
 bool direct_selected()const override{return state.selected;}
 void command_word(unsigned level,u16 value,u16 mask) override {
  assert(level==4 && mask==0xffff);
  assert(state.data(value,level,mask));
 }
 void output_data_masked(unsigned level,u16 value,u16 mask) override {
  assert(level==4 && mask==0xffff);
  assert(state.data(value,level,mask));
 }
};
int main(){
 p6066_bus_device bus;
 p6066_go011_device video;
 console_card console;
 p6066_pr6610_device paper;
 bus.m_cards[5]=&console;
 bus.m_cards[8]=&video;
 video.device_reset();
 // Console selection must not also select the video; printer commands
 // and display strobes go through the real production backplane dispatch.
 bus.select_w(0);
 assert(console.direct_selected() && !video.direct_selected());
 bus.command_w(4,0xff00,0xffff);
 assert(console.state.printer_running && console.state.printer_attached);
 bus.data_w(4,0x2021,0xffff);
 assert(console.state.display_strobes==1 && console.state.display[0]==0x21);
 // ESE switches to the separate GO011 slot, without changing GOINO state.
 bus.select_w(0xff);
 assert(!console.direct_selected() && video.direct_selected());
 bus.command_w(4,0x0000,0xffff); // GO011 address zero
 bus.data_w(4,0x5480,0xffff); // video first pixel lit, streaming byte
 assert(video.m_state.diagnostic_pixel(0,0));
 assert(!video.m_state.diagnostic_pixel(1,0));
 assert(console.state.printer_running && console.state.display_strobes==1);
 // Printer's pending request survives GO011 activity, and servicing it
 // does not alter the independently stored GO011 framebuffer.
 paper.tick(console.state);
 assert(console.state.column_request);
 console.state.synchronize(4);
 assert(console.state.acknowledge(1));
 assert(console.state.data(1,2));
 console.state.end(2);
 paper.tick(console.state);
 assert(console.state.printer_columns_discarded==1);
 assert(paper.m_columns_rendered==1 && paper.bitmap.pixels.size()==4);
 assert(paper.bitmap.pixels[0]==std::make_tuple(28,24,0U));
 bus.select_w(0);
 assert(!video.direct_selected() && console.direct_selected());
 assert(video.m_state.diagnostic_pixel(0,0));
 assert(console.state.display[0]==0x21);
 std::puts("PASS: simultaneous GO011 and PR6610 output; production bus, video and printer render methods retain independent state");
}
"""
with tempfile.TemporaryDirectory(prefix="p6066-pr6610-video-") as directory:
    cpp = Path(directory) / "test.cpp"
    binary = Path(directory) / "test"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-O2", "-I", str(ROOT / "src/devices"),
                    str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
