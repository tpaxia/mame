# P6066 bring-up branch

Work started 2026-09-17 on branch `P6066`, based on upstream master
`315fa5f0aa5f8288428e8773ff061e228d525089` (verified against the remote).
The checkout is shallow; the full upstream history is not downloaded.

The machine will be `p6066`, with a reusable CPU core under `cpu/puce`.
CPU19 is the documented hardware name; PUCE1 and PUCE2 are its boards.
The driver will execute original PUCE firmware rather than reproduce the
Z8000 EP60 implementation of the program-visible instruction set.

The local parent project contains `MAME-P6066-IMPLEMENTATION-PLAN.md` with
the complete architecture, evidence references and acceptance gates.

## Current state

The `P6066` branch is published at https://github.com/tpaxia/mame/tree/P6066.
`origin` is the tpaxia fork; `upstream` is mamedev/mame.

This is an early M0/M1 increment, **not a bootable P6066**.

* Added `puce_device` and the `p6066` development machine. The CPU executes
  a documented subset of register/logic/branch operations and basic AMI/MAI
  byte transfers. Unsupported operations stop with a diagnostic.
* Scratchpad-backed counters implement full-width level 3/4 and short
  level 1/2 addressing. Reset selects level 3 at word 8000; fetch advances
  the selected counter before execution. COM0/COM1 currently model internal
  context changes only; external ECORN and interrupt circuitry remain absent.
* The live architectural state is shared with standalone tests. It is not
  a separate reference emulator. Counter boundaries, register aliasing, flags,
  byte ordering and unsupported operations have focused regression checks.
* The reference CAROM cold path reaches its first external-channel operation,
  ESE at word 808B. An isolated entry at 8003 runs the first register self-test
  block to COM3 at 802C in 196,731 instructions. This bypasses console setup
  for that test only and does not establish a complete CAROM self-test.
* The partial disassembler has 44 reference examples and exhaustive single-word
  fetch/length checks. Branch and AND decoding is added; COM2 is left `DW`
  because the documented command table omits it. Many executable instructions
  still display `DW` until their disassembler entries are transcribed.
* ROM inventory tooling records hashes. No ROM bytes are distributed here.

The machine uses a provisional lower 64KB RAM map and the merged 4KB CAROM
reference at word 8000. Physical ROM mapping/revision and RAM-board population
remain unresolved. Two scheduling quanta per instruction and a 1MHz device
clock are placeholders, **not hardware timing**. Only established reset
state is changed on reset; remaining registers start at deterministic zero
on initial construction, with no claim that hardware clears them.

Bus slots, memory boards, DMA, external interrupt requests/arbitration, GOINO,
FLODI and GISA2 are not implemented. Save items include execution phase and
architectural state, but save/restore integration remains untested. No complete
CAROM or bootstrap gate has passed.

## Sources for this increment

Olivetti CPU19 *Tabella Microistruzioni*, publication 801.30.1:

| V2 PDF page | Printed page | Implemented evidence |
|---|---|---|
| 2–3 | 2.098–2.099 | Byte transfers, AND, command table (no COM2) |
| 5, 7 | 2.101, 2.103 | Decrement, increment and register verification |
| 10, 12–13 | 2.106, 2.108, supplement | Branches, register/DI transfers and shifts |
| 4 | 2.100 | CRTA/CRTB memory versus RO encodings; COM7–15 |
| 9 | 2.105 | NOP, REDI, OR/XOR operations, rotates, SAB; RESE is hardware-generated |
| 11 | 2.107 | SEDI, shifts and SEI/SEIM/SEIP |
| 14 | 2.109 | INCD0 and INCD1 supplement |

SUCE2 description PDF p.14 separately confirms COM0/COM1 memory encodings.
The COM family names are supplied by the CPU19 command tables; this
disassembler does not attempt to describe each command's hardware effects.
CPU19 description PDF p.9 defines even byte addresses as the high byte of
a memory word; scratchpad A is the low byte of L. The state helper keeps
these two byte conventions separate.

The decoder deliberately recognizes only the transcribed operand patterns;
unverified don't-care variants remain undecoded.

## ROM inventory findings

Run from this checkout:

```sh
python3 scripts/puce/audit_sources.py --project .. --output ../analysis/mame-p6066
```

The current inventory covers 18 ROM/media files and six core manuals. It
records size, CRC32, SHA-1 and SHA-256. Re-running hashes the current inputs;
it does not treat an old filename as a checksum or assume the inputs match
the original audit.

The two upstream merged CAROM/ROMCA files are identical 4096-byte images,
CRC32 `9caca305`. The local AndreaMaglio set has five 4096-byte ROMCA files
and two 8192-byte updated CAROM files, none byte-identical to that merged
upstream image. Their filename CRCs match their computed CRCs. Their exact
physical organization and revision relationships are still unresolved.

The GISA2 82S123 dump is 32 bytes, CRC32 `a7aabf03`. Video dumps are also
inventoried, but are not required for the small display's firmware-supplied
dot patterns. Master disk 068 is generation material, not yet selected as a
boot fixture. The actual starting boot disk remains to be selected.

## Build and tests

Standalone decoder test, requiring only a C++20 compiler and Python:

```sh
python3 scripts/puce/test_disassembler.py
```

On this Apple Silicon host, SDL2 is installed through Homebrew. The current
upstream macOS default uses SDL3, which is not installed. Select the supported
SDL2 backend explicitly for the tools build:

```sh
make TOOLS=1 EMULATOR=0 IGNORE_GIT=1 OSD=sdl USE_LIBSDL=1 SDL_INSTALL_ROOT=/opt/homebrew REGENIE=1 -j4
```

`IGNORE_GIT=1` avoids a `git describe` warning in this shallow untagged
checkout; the exact base revision is recorded above. The main emulator is
not built by this command. The top-level makefile has no standalone
`unidasm` target; build the tools or use its generated submake after generation.

After the tools build:

```sh
python3 scripts/puce/test_disassembler.py --unidasm ./unidasm
./unidasm ../upstream-olivetti-p6060/rom/system/carom/carom.bin -arch puce -basepc 8000 -count 16
```

The first words should show `CRTB B0,C80`, `CRTA A0,C89`, `COM0` at consecutive
word addresses 8000, 8001, 8002. Other entries can legitimately remain `DW`
until their opcode families have been transcribed and tested. An address-correct
listing is not a test of CPU execution.

## CPU build and tests

```sh
python3 scripts/puce/test_cpu.py
python3 scripts/puce/test_cpu.py --carom ../upstream-olivetti-p6060/rom/system/carom/carom.bin
make SUBTARGET=p6066 SOURCES=src/mame/olivetti/p6066.cpp OSD=sdl USE_LIBSDL=1 SDL_INSTALL_ROOT=/opt/homebrew IGNORE_GIT=1 REGENIE=1 -j4
```

The optional CAROM test requires SHA-1
`63a68b4787f33bef156f05c6b50269357d0d3c2f`. It checks cold-path control flow
using a minimal memory fixture and separately enters the register test block.
It does not exercise the MAME memory system; a headless machine run is the
additional integration check. Supply the same image as `p6066/carom.bin`
inside your own ROM directory:

```sh
./p6066 p6066 -rompath /path/to/roms -video none -sound none -nothrottle -skip_gameinfo -seconds_to_run 1
```

For this increment the expected diagnostic is an intentional nonzero exit:
`PUCE bring-up: unsupported B1F4 at word 808B (level 4, next PC 808C)`.
This is an implementation stop, not an emulated hardware trap.

## Next implementation steps

1. Resolve reset ROM revision/physical mapping and RAM-board population.
2. Implement the GOINO external-channel interface needed by cold CAROM before
   the register self-test, including ESE selection and console lamp output.
3. Extend CPU execution and decoder coverage for the remaining self-test,
   memory and peripheral operations. Verify command side effects in hardware.
4. Implement the plan's interrupt/DMA/arbitration, floppy and serial milestones,
   with display/keyboard integration and a verified bootstrap disk.

Board clocks, physical priority order, DMA boundary jumpers and initial boot
disk remain open decisions. CPU19-family capabilities must not be mistaken
for the final P6066 configuration.
