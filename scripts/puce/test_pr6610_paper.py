#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""Exercise the production PR6610 paper callback with independent roll stimuli."""

from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
text = (ROOT / "src/devices/bus/p6066/pr6610.cpp").read_text()
signature = "u32 p6066_pr6610_device::screen_update("
start = text.index(signature)
opening = text.index("{", start)
end, depth = opening + 1, 1
while depth:
    depth += (text[end] == "{") - (text[end] == "}")
    end += 1

source = r"""
#include <cassert>
#include <cstdio>
#include <vector>
using u32 = unsigned;
struct screen_device {};
struct rectangle { int min_x, max_x, min_y, max_y; };
struct bitmap_rgb32 {
 static constexpr int WIDTH = 1736, HEIGHT = 1271;
 std::vector<u32> pixels = std::vector<u32>(WIDTH * HEIGHT, 0x123456);
 u32 &pix(int y, int x) { return pixels.at(y * WIDTH + x); }
};
struct bitmap_printer_stub {
 static constexpr int RING = 1890;
 int m_ypos = 18;
 std::vector<u32> roll = std::vector<u32>(RING, 0xffffff);
 int get_pixel(int x, int y) {
  assert(x >= 0 && x < bitmap_rgb32::WIDTH);
  int row = (y % RING + RING) % RING;
  return roll.at(row);
 }
};
struct p6066_pr6610_device {
 static constexpr int PAPER_SCREEN_HEIGHT = 1271;
 bitmap_printer_stub storage, *m_bitmap = &storage;
 u32 screen_update(screen_device &, bitmap_rgb32 &, const rectangle &);
};
"""
source += text[start:end] + "\n"
source += r"""
int main() {
 p6066_pr6610_device card;
 screen_device screen;
 bitmap_rgb32 pixels;
 // The head at y=18 prints at row 1221. Both earlier lines and later
 // paper-feed positions must map to the same physical ring pixels.
 card.storage.roll[18] = 0;
 card.storage.roll[3] = 0xabcdef;
 card.storage.roll[1888] = 0x112233;
 rectangle full{0, bitmap_rgb32::WIDTH - 1, 0, bitmap_rgb32::HEIGHT - 1};
 card.screen_update(screen, pixels, full);
 assert(pixels.pix(1221, 28) == 0);
 assert(pixels.pix(1206, 28) == 0xabcdef);
 assert(pixels.pix(1201, 28) == 0x112233);
 assert(pixels.pix(1220, 28) == 0xffffff);
 // Ten manual-feed steps move the print point 35 rows without moving ink
 // in the storage ring: previously printed ink travels upward on screen.
 card.storage.m_ypos += 35;
 card.screen_update(screen, pixels, full);
 assert(pixels.pix(1186, 28) == 0);
 assert(pixels.pix(1171, 28) == 0xabcdef);
 // Wrap after many lines: the newest ink is still 50 px above the bottom.
 card.storage.m_ypos = 1901;
 card.storage.roll[11] = 0x246810;
 card.screen_update(screen, pixels, full);
 assert(pixels.pix(1221, 28) == 0x246810);
 // A clipped update must leave all pixels outside the clip untouched.
 pixels.pix(0, 0) = 0x777777;
 card.screen_update(screen, pixels, {28, 28, 1221, 1221});
 assert(pixels.pix(0, 0) == 0x777777);
 std::puts("PASS: production PR6610 screen callback, feed, roll wrap and clip");
}
"""
with tempfile.TemporaryDirectory(prefix="p6066-pr6610-paper-") as directory:
    cpp = Path(directory) / "test.cpp"
    binary = Path(directory) / "test"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-O2", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
