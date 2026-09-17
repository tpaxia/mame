<!-- copyright-holders: Salvatore Paxia -->

# COM3 and initial input-bus checks

Cold CAROM now executes COM3 at word 802C and the input checks at 802D–8035.
It stops before executing ADDA A3,B2 (`9632`) at word **8039**, with next PC
803A. This is further progress through self-test, not completion of it.

## CPU behavior

COM3 asserts the active-low ECORN line and leaves the CPU registers, DI and
execution level unchanged. The line remains asserted across following
instructions. COM0 releases ECORN when exiting a level; COM0 at level 4 does
nothing, as documented. ECORN is saved as CPU state, exported to the debugger,
and emitted through a callback on transitions and restoration. The current
machine exposes the raw line as the `ecorn` output; there are no external
controller boards attached to receive it yet. Initial machine reset asserts
the line as a development initialization choice; its detailed hardware reset
sequencing remains to be verified.

The following input instructions now read callbacks without generating ECOT:

| Instruction | Opcode | Result |
|---|---|---|
| ENTL L(x) | AAx0 | Name in A(x), type in B(x) |
| ENUA A(x) | B9x0 | Name in A(x); B(x) preserved |
| ETIB B(x) | B2xF | Type in B(x); A(x) preserved |
| EDA A(x) | B8x8 | Data in A(x); B(x) preserved |
| EDB B(x) | A9x8 | Data in B(x); A(x) preserved |

None changes DI. Tests use nonzero input values to check packing, preservation
of the other register half, and flags independently of CAROM's expected zero.
Decoder coverage is now 56 reference cases plus exhaustive word-length checks.

## Current bus model and its limits

This development machine has no external controller boards or interrupt
owners. GOINO's direct selection is masked outside level 4. Consequently the
level-3 CAROM checks currently sample **undriven** name/type/data buses, modelled
as logical zero. This is a functional default, not a fabricated response from
a floppy or serial controller, and not a full electrical bus simulation.

The GOINO manual explicitly describes an undriven name bus reading as 00.
The type/data default and CPU-name jumper population still need independent
schematic verification. CAROM masks name bit 1 before checking zero, so the
successful test cannot distinguish a CPU name of 00 from 02. Its later data
comparison also cannot independently establish every undriven electrical
level. These remain open evidence items; no PC-specific responses or skipped
failure branches have been added.

Selected level-4 GOINO input remains unsupported and reports an error. Do not
use the current zero defaults as a substitute for selected-device responses,
interrupt ownership, acknowledgement, or bus arbitration. Input callbacks
must be routed through the future bus implementation as controllers are added.

COM3 does not call MAME's device reset on GOINO/CONDY. The available console
block diagram separates selection and local display/lamp storage, and does
not establish that ECORN clears these buffers. We preserve the console
latches rather than inventing that connection. The lamp register remains
FFFF through this increment. Full controller reset fan-out and card-specific
side effects remain future work.

## Evidence

* CPU19 *Tabella Microistruzioni V2*, PDF p.3: COM0/COM3 and ECORN.
* Same manual, PDF pp.5–6 and p.20: EDA/EDB, ENTL/ENUA/ETIB and operand halves.
* GOINO/ASTAM/CONDY/ROMCA/TASTIERA description, PDF pp.7–8, 10 and 16:
  console block diagram, direct selection masking, undriven name bus and
  interrupt-owned selection.
* Original merged CAROM, words 802C–8035: independent firmware check of reset
  bus state. Its expectations are validation evidence, not a replacement for
  verifying the physical wiring.

Manuals are in the parent project's
`AndreaMaglio/DocumentazioneP6060_P6066/Manuali STAC e di servizio/2L`.

## Validation and next step

The standalone CPU and decoder tests pass. The MAME integration test in
`scripts/puce/test_console_mame.lua` verifies ECORN held low, L9=0000 and
L10=FD00 at the new stopping point, 256 lamp strobes, synthetic lamp/display
output with level-4 COM0 leaving reset asserted, and panel restart followed by
the same cold path. All three PASS markers must be present; Lua assertions
alone do not guarantee a nonzero MAME exit status. Save/load re-drive is
implemented but its integration test remains outstanding.

The next CPU work is ADD/SOT arithmetic, including carry and nibble flags,
followed by the word-memory operations later in CAROM. The physical bus
verification and controller implementations remain separate open gates.
