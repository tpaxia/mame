<!-- copyright-holders: Salvatore Paxia -->

# ME006 and the disk-to-firmware bootstrap gate

Original CAROM now loads archive disk 121 (P6060 assembler) and transfers to
its disk-loaded firmware at **word 1000, level 3**. This is a verified loader
handoff, not completed ESE startup or evidence that this disk is a P6066 ESE master.

## Hardware evidence

Source: `RA008 e ME006 - RAM per CPU19M - Descrizione di Funzionamento.pdf`,
in the parent project's `AndreaMaglio/DocumentazioneP6060_P6066/Manuali STAC e di servizio/2L`.

* PDF p.7 identifies ME006 as P6066 microprogram memory.
* PDF p.37 (printed 2.01) specifies 32 KB, four selectable word-address
  windows (0000–3FFF, 4000–7FFF, 8000–BFFF, C000–FFFF), and exclusion of
  the first one or two 2 Kword banks through jumpers.
* PDF p.39 (printed 2.02) describes sequential-read lookahead and restricts
  P6066 ME006 to the CPU zone, excluding DMA accesses.

The separate `p6066_me006_device` supplies storage and jumper-controlled decode.
The removable `bus:microcode` slot defaults to ME006 at window 8000–BFFF,
with the first 2 Kwords excluded. Thus RAM responds at **8800–BFFF** and
coexists with the merged CAROM image at **8000–87FF**. These are word addresses:
28 KB of the board's 32 KB is visible. Lower memory remains 64 KB on four
separate configurable RAM cards. There is no ROM overlay or hidden RAM.

The chosen population is a documented possible decode configuration, not a
verified factory chassis inventory. Lookahead timing and parity are not
implemented; DMA itself is absent and future DMA must enforce the CPU-zone
restriction. Slot position is a functional ordering, not a physical connector claim.

## Independent load verification

The test reads IMD records independently on the host to construct expected bytes.
The emulated controller still uses MAME floppy flux, interrupts and guest firmware.
The disk's cylinder 1, sector 1 descriptor requests:

| Record | First cylinder/sector | Destination word | Sectors | Bytes |
|---|---|---|---|---|
| D | 01/02 | 0180 | 43 | 5,504 |
| D | 02/19 | 1000 | 8 | 1,024 |
| D | 03/01 | A000 | 128 | 16,384 |
| D | 11/16 | 0C40 | 4 | 512 |

The intervening space-tagged record at 7C00 is not an initial D load. The final
F record selects entry word 1000. All **23,424 bytes** match guest RAM at entry.
The controller reports **206 sectors**: 22 directory sectors, one descriptor,
and 183 payload sectors. CAROM's memory enumeration produces 16 invalid
probes with ME006 installed, compared with 30 without it.

```sh
python3 scripts/puce/test_bootstrap.py \
  --rompath ../analysis/mame-p6066/roms \
  --media ../analysis/mame-p6066/media/p6060-assembler-121.imd \
  --results ../analysis/mame-p6066/bootstrap-validation
```

Run from the MAME checkout. The test checks the fixture SHA-256:
`9208fda4c8ecc74cba0633c1a38380e5412e79a85b5a73b6e151e8be4f257776`.
A read-only instruction-fetch observer compares RAM and requests emulator exit
at the natural entry checkpoint. It does not write registers, substitute data,
patch firmware or change the PC. A re-entry guard prevents debugger reads of
word 1000 from recursively invoking validation. The removed-board case also
requires the trace to report an unclaimed write at A000 and no firmware entry.

Both cases pass. Results contain logs, traces, expected/loaded blocks, and
`installed.png` / `removed.png`. The installed snapshot shows all console
lamps off at handoff, after the earlier first-read FFFF pattern.

## Next boundary: GOINO initialization

Without the entry observer stopping execution, firmware continues and issues
F400 through DAE at level 4; the unsupported-output diagnostic reports next
PC 1085. The startup sequence includes F4/F5/F6/F7/F8/F9/FB/FD/FE commands.

The GOINO description PDF p.9 (printed GOINO 5, figure 1.3) lists command
04 as REMAN and other reset/control commands, but shows the upper nibble zero.
PDF p.17 (printed GOINO 13) identifies ECD8–ECDB as command decode, ECDD/ECDE
as display/lamp strobes, and cites DISL002 C2/E2 for command circuitry.
These establish command functions but do not yet justify treating F4 as 04
or suppressing possible simultaneous strobes. No masking or no-op workaround
has been added. Resolve the upper-bit gating against circuit/revision evidence
before implementing this startup path. See [remaining evidence](../../../TBD.md).
