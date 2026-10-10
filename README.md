# MAME branches

Custom branches built on top of MAME, grouped by machine.

`*` = already merged upstream into mamedev/mame (PR #15866, PR #15883 and PR #16342).

## Zilog Z8000 test rig

- `z8k_test_harness`: Z8000 instruction test rig (`z8ktest01` Z8001, `z8ktest02` Z8002 machines).

## Zilog Z8002 homebrew machines

- `z8002-demo`: Z8002-demo CP/M machine.
- `z8002-plasmo`: Plasmo homebrew Z8002 machine.

## Unix V7 demonstration machines

- `unixv7_demo`: both `z8001unix` (Z8001) and `z8002unix` (Z8002), with custom paged MMUs, separate instruction/data spaces and disk-loaded Unix V7. Replaces the separate `z8001_unix` and `z8002_unix` branches.
- The machines share console, clock and disk interfaces and run the same user executables; each uses its own kernel and boot ROM. A shared board implementation and separate CPU-specific files live under `src/mame/homebrew/`; the `unixv7_demo` executable contains both machines.
- Kernel, firmware, disk preparation and build/run instructions are maintained in [z8000_unix](https://github.com/tpaxia/z8000_unix); Z8002 bring-up is on its `z8002-mmu` branch.
- Always launch either machine with `-window`.

## Olivetti M20

- `m20_hd_and_fixes`*: WD1000 hard disk, keyboard bell and serial fixes, save states and driver fixes (segment trap, marked working).
- `m20_biosx`: local BIOS 2.0x (2.0f plus ROM monitor, 16 KB).

## Olivetti M40 / L1

- `olivetti_m40`: M40 driver.
- `m40_z8010_sup_test`: GO363 hard disk work plus Z8010 MMU suppression tests.

## Olivetti P6066

- `P6066`: P6066 driver, with console, printer and hard disk work and notes under `docs/p6066`.
- `m40_p6066`: P6066 work combined with the M40 branch.

## Zilog System 8000

- `s8000_fixes`*: general fixes.
- `s8000_h19`: H19 as the console terminal.
- `sadie`*: SADIE diagnostics, SMDC emulation and Z80 SCC loopback fix (mamedev/mame PR #16342).

## Zilog MCZ

- `MCZ`: work in progress.
