<!-- copyright-holders: Salvatore Paxia -->

# FLODI first-read milestone

The first-read acceptance gate passes with original, unmodified CAROM and
archive disk 121 (P6060 assembler). CAROM selects drive 2, homes the head,
counts the settling interval, synchronizes to index, reads track 0 sectors
5–26, and receives the level-3 end interrupt. Every one of the 2,816 payload
bytes has been compared with the IMD source **after the CPU wrote it to RAM**.
This is not yet a complete floppy controller or a booted P6066/ESE system.

The CPU performs the transfers through level-1 interrupts and EMIP. This is
not DMA. MAME's IMD loader supplies drive flux; the controller neither parses
IMD nor injects extracted firmware or directory records into guest RAM.

## Circuit evidence that resolved the failed command

The local `reference/AdreaRiccardoEmanuele/FLODI FLOA-FLOB SCHEMI LOGICI
(BIT 661.60.1 G.02) .pdf` contains logical sheets titled **FLOD2**, drawing
168664. They must not be treated as identical to the older FLODI description.

* PDF p.5, sheet 002 (168664-K02), E9: command-register output CATEO is
  parallel input ECD70 (command bit 7). CATEN is its complement; G9 buffers
  it as CATE2. The status circuit uses the CATE command signal labelled CATE5.
* PDF p.11, sheet 007 (168664-K07), E5/E6: two NAND gates implement
  `TEVEO = (DIVE0 AND RIF10) OR NOT CATE5`.
* Sheet 007 E8/E9 routes TEVEO through the function/end-status multiplexer
  onto EPD2. The bus line is active-low; the CPU callback uses logical bits.
* Thus positioning commands 40/50, with CATE clear, report function-status
  bit 2 set. Command 80 sets CATE and clears that term. The earlier model
  incorrectly returned zero for positioning, indistinguishable from a sector
  count. CAROM consequently decremented its sector counter during head steps.

K02 P9 also presets COTE0 while CATE2=0. K07 C8/C9 routes COTE0 to
logical EPD1. Positioning therefore reports **06**, or **46** at track zero
(bits 1 and 2 plus PIZE); the earlier 04/44 model omitted COTE0. CAROM
then emits 80, counts ten time events, and emits 80/00 to start reading.
Its final-sector command and end interrupt now occur. This change follows
these gates; it is not a firmware-specific replacement for head movement.
Physical board/revision matching for the eventual P6066 configuration remains
an inventory task in [TBD.md](../../../TBD.md).

## Timing and transfer evidence

Unless otherwise specified, references are to PDF page numbers.

| Source | Evidence used |
|---|---|
| CPU19M supplement inside RA008/ME006, p.89, printed 3.12 | UC020 L03 independently selects reset at 8000/C000 and level-1/2 pages at 81XX/82XX or C1XX/C2XX. Implemented and regression-tested; ordinary short data addressing is separate. |
| CPU19 microinstruction table V2 pp.5–6, 8, 20 | ECO, EMI/EMIM/EMIP, MEI/MEIM/MEIP; COM7/8 and EDC channel control. |
| CPU19 V2 p.12 | TABC is a separate SUCE2 service-console transfer, not GOINO lamps. |
| FLODI pp.15–17, 32 | Selection names, interrupt types, status and commands. Status bit 2 is qualified by the later FLOD2 circuit above. |
| FLODI pp.21–22, printed 33–36, figures 16–17 | Eight ID interrupts; FE/track/head/sector/size comparison; ECOFO/FITUCO/FNCO/COCI byte pipeline, payload and CRC windows. |
| FLODI pp.7–9, 19–20 | Head positioning, firmware time counting, sector-count and index events. The controller time counter emits events every 4 ms until commanded out of that phase. |
| FDU STAC 1L p.12, printed 1.06, fig.9 | Both mains-frequency versions turn the spindle at 360 RPM. |
| FLODISC/ALI161 pp.15–16, printed 9–10, figs.14–18 | Two motor pulses per track. First pulse spacing T1+T2 = 1.5+5 ms, subsequent spacing 5 ms. Model first track-completion event at 6.5 ms, subsequent events at 10 ms. |
| FLODISC/ALI161 p.28, printed 22 | FM clock spacing 4 us, clock-to-data spacing 2 us. |
| FLODISC/ALI161 p.38, printed 32 | Read discriminator/PLL synchronization; the current nominal-cell decoder does not reproduce its analogue behavior. |

Byte deadlines advance from the preceding physical deadline, not from the end
of CPU service. Mechanical timers similarly advance independently of COM0.
The CRC falling-edge pipeline ends the byte train; sector size alone does not
terminate the transfer. The controller reads both recorded CRC fields and
preserves deleted-data marks. Index comes from the MAME drive callback.

## Reproducible acceptance test

Build the `p6066` subtarget, supply the reference CAROM and archive disk 121,
then run from the MAME checkout:

```sh
python3 scripts/puce/test_flodi.py --rompath /path/to/roms \
  --media /path/to/121.IMD --results /path/to/results
```

The independent test parser reads IMD solely to construct an oracle. Lua
read taps observe CAROM's CRC-service and end-handler checkpoints; they return
no replacement data and never write RAM. CAROM reuses byte addresses 0080–00FF
for each sector, so a single dump after the command cannot contain all 22
sectors. Each buffer is captured before reuse and the captures are concatenated.

Validated fixture SHA-256:
`9208fda4c8ecc74cba0633c1a38380e5412e79a85b5a73b6e151e8be4f257776`.

| Case | RAM comparison | End status | Panel |
|---|---|---|---|
| Original disk | 22 sectors, 2,816 bytes, all equal | 80: deleted-data mark on sector 26, no CRC error | FFFF |
| Test copy with first payload's IMD CRC-error flag set | Same 2,816 bytes, all equal | C0: CRC error plus deleted-data mark | FFFF |

The error copy retains every payload byte. MAME converts its IMD error flag
into a bad recorded CRC; FLODI must detect it from flux and report the changed
status. Original media are never edited. Tests leave logs, traces, captured
payloads and PNG console views in the results directory.

## Beyond this gate and remaining limitations

The later write to word A000 is now supplied by a separate removable ME006
board using its documented address window and bank exclusions. Original CAROM
loads 23,424 bytes and transfers to disk firmware; see
[ME006 bootstrap acceptance](me006-bootstrap.md). Removing that board reproduces
the unclaimed A000 write. A pending invalid-data event no longer falsely marks
an intervening valid level-1 fetch at 8173 as invalid. Full internal invalid-cycle
handling remains an evidence item; this fix does not claim complete coverage.

The functional read path is not a complete electrical model. Open items include
PLL/jitter/weak cells, ready/index gating during spin-up, media changes, missing
marks, simultaneous event latches, overrun behavior, exact CPU/bus timing,
write/format/scan commands and save/restore verification. Unsupported command
and overrun cases currently stop with diagnostics. See [TBD.md](../../../TBD.md).
The archived `analysis/mame-p6066/flodi-unverified-draft/` is historical
investigation material, not the current implementation.

Zero command bytes now clear mechanical VIRI/CATE activity as well as byte
reads (K02 E8/E9, FLODI table 5). The original disk firmware reaches track
zero and sends this clear, but subsequently waits at A0F9; the parent
project's `analysis/mame-p6066/firmware-startup-dispatch.md` records the
unresolved control-block layout. This is not completed OS startup.
