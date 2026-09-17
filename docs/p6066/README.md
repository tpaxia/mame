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

This is the first M0/M1 development increment, **not a runnable P6066**.

* Added a partial PUCE disassembler and `unidasm -arch puce` integration.
* Uses 16-bit words and word-addressed PCs; upstream merged ROM files are
  read big-endian by unidasm. This does not determine physical chip interleave.
* Decodes the visually checked CRTA/CRTB, INCD0/INCD1, SEDI/REDI/NOP,
  OR/XOR families, SAB, selected shifts/rotates, SEI/SEIM/SEIP and COM forms.
* Untranscribed words display as `DW xxxx`, preserving their value and length.
  This is incomplete coverage, not a claim that those opcodes trap in hardware.
* RESE and ALFA are not invented as memory instructions from their internal RO
  values. They belong in the future CPU execution sequencer.
* Added a ROM-free test that compiles the actual MAME disassembler and checks
  40 independent examples plus fetch/length invariants over all 65,536 words.
* Added reproducible local-source inventory tooling. No ROM bytes are added
  to this branch.

CPU execution, bus slots, memory boards, DMA, interrupt logic, GOINO, FLODI
and GISA2 are not implemented yet. None of the CAROM or bootstrap gates has
been passed. The partial disassembler must be extended before treating an
entire ROM listing as decoded code.

## Sources for this increment

Olivetti CPU19 *Tabella Microistruzioni*, publication 801.30.1:

| V2 PDF page | Printed page | Implemented evidence |
|---|---|---|
| 4 | 2.100 | CRTA/CRTB memory versus RO encodings; COM7–15 |
| 9 | 2.105 | NOP, REDI, OR/XOR operations, rotates, SAB; RESE is hardware-generated |
| 11 | 2.107 | SEDI, shifts and SEI/SEIM/SEIP |
| 14 | 2.109 | INCD0 and INCD1 supplement |

SUCE2 description PDF p.14 separately confirms COM0/COM1 memory encodings.
The COM family names are supplied by the CPU19 command tables; this
disassembler does not attempt to describe each command's hardware effects.
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

## Next implementation steps

1. Resolve the reset ROM revision and physical mapping, using the CAROM board
   documentation and local dumps. Do not concatenate dumps merely to fill a
   chosen region size.
2. Complete the decoder/evidence table, especially branches, memory transfers,
   arithmetic and all flag/command variants reached by CAROM self-test.
3. Implement `puce_device`: scratchpad-backed counters, DI, hardware reset/fetch
   and resumable execution phases. Verify byte/word and short-register addressing.
4. Add the minimal `p6066` machine with evidence-backed ROM/RAM configuration,
   then the GOINO console needed to observe and complete self-test.
5. Follow the plan's interrupt/DMA/arbitration, floppy and serial milestones.

Board clocks, physical priority order, installed RAM population, DMA boundary
jumpers and initial boot disk remain explicit open decisions. CPU19-family
capabilities must not be mistaken for the final P6066 configuration.
