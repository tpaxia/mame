# MAME branches

Custom branches built on top of MAME, grouped by machine.

`*` = already merged upstream into mamedev/mame (PR #15866 and PR #15883).

## Zilog Z8000 test rig

- `z8k_test_harness`: Z8000 instruction test rig (`z8ktest01` Z8001, `z8ktest02` Z8002 machines).

## Olivetti M20

- `M20_kbd_serial`*: keyboard bell and serial fixes.
- `M20_savestate`*: save state support.
- `M20_HD`*: WD1000 hard disk controller (the last commit, format buffer init, is not merged).
- `m20_hd_and_fixes`*: hard disk plus driver fixes (segment trap, marked working).

## Olivetti M40 / L1

- `olivetti_m40`: M40 driver.
- `olivetti_m40_hd`: experimental GO363 hard disk support.
- `m40_z8010_sup_test`: GO363 hard disk work plus Z8010 MMU suppression tests.
- `m40_debug_instrumentation`: debug instrumentation and Z8000 CPU fixes.
- `mdos`: MDOS boot debugging on the M40 (probes and FDU/VRAM tracing), plus Z8000 CPU fixes.

## Zilog System 8000

- `s8000_fixes`*: general fixes.
- `s8000_h19`: H19 as the console terminal.
- `s8000_smdc_interrupt_handshake`: keep the queued SMDC completion interrupt handshake.
- `z8010_bus_interface`: Z8010 MMU bus interface.
- `sadie`: SADIE diagnostics, SMDC emulation and Z80 SCC loopback fix.
- `z8010_sadie_stack`: same as `sadie`.

## Zilog MCZ

- `MCZ`: work in progress.
