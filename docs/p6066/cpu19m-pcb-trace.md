<!-- copyright-holders: Salvatore Paxia -->

# CPU19M address selection: PCB and connector investigation

## Result

The missing level-1/2 instruction-page rule is documented. It does not need
to be guessed from CAROM placement or inferred solely from PCB traces.

Source: `RA008 e ME006 - RAM per CPU19M - Descrizione di Funzionamento.pdf`,
PDF page 89, printed page 3.12, publication 801.30.1 (03). This file contains
a CPU19M supplement starting after the RAM material; its filename concealed
its relevance to CPU implementation. The scanned page was checked visually.

| Configuration shown | Level-3 reset entry | Level-2 instruction page | Level-1 instruction page |
|---|---|---|---|
| First diagram | 8000 | 82XX | 81XX |
| Second diagram | C000 | C2XX | C1XX |

The page identifies **PIASTRA UC020, position L03**. In the second diagram,
separate arrows associate switch 1 with the reset address and switch 2 with
the bracket covering both interrupt pages. Both are off in the first diagram
and on in the second. Switches 3 and 4 are drawn off in both cases; their
purpose is not established here. Do not assign a function to them by analogy.

The diagram supplies architectural behaviour; it is not a complete electrical
netlist. In particular, it does not identify the connector contacts carrying
each resulting BC bit or the gates between the switches and address outputs.

## PCB observations

The P6066-specific `UC20 A.pdf` component scan shows a blue four-position
switch bank near the centre of the board and the PROM labels CR10, CR20,
CR30 and CR40. The solder-side `UC20 B.pdf` scan shows individual traces and
vias. These are suitable starting points for a pin-by-pin reconstruction.
The identity of the switch bank is consistent with the manual's L03 drawing;
an electrical route from its pins to BC14 has **not** been established.

PUCE1/PUCE2 photographs in the P6060 directory belong to a different labelled
board pair. They must not substitute for UC19/UC20 trace evidence without
checking the revisions and connections.

## Connector wiring available so far

There is **no verified complete CPU/memory backplane contact-to-signal table
located in this investigation**.

* The reconstructed GISA2 schematic provides left/right backplane connector
  drawings with A/B contact numbering. Many CPU-side nets retain generic
  PIN labels. This does not identify the CPU's BC00–15 contacts and does not
  establish that every CPU/memory slot has the same contact assignments.
* The SUCE2 description identifies BC00–15 and explains how its address
  display captures that bus. Those signal names do not themselves give the
  edge-contact numbers.
* The ROMCA description, combined GOINO/ASTAM/CONDY/ROMCA manual PDF p.66,
  describes BC15–11 module decode and BC10/BC09 selection. It refers to
  P6060 logical-schematic collection 670.01.0.2, DISL01. This is a useful
  source to locate for named address nets, but is not a connector pinout.

## Trace record and next evidence needed

| Path | Current evidence | Status |
|---|---|---|
| UC020 L03 selection to reset/interrupt address ranges | CPU19M p.3.12 | Documented architectural effect |
| CPU BC00–15 to SUCE2 address-display latch IM00–15 | SUCE2 p.2.20 | Documented signal-level path |
| UC020 switch pins through gates to BC08–15 | Front/back PCB scans | Not yet reconstructed |
| BC08–15 to numbered backplane contacts | No named connector anchor found | Unresolved |

A future net trace must record board orientation, package coordinate, pin
number, vias and both-side segments. Covered or indistinct segments remain
unknown until another drawing or a continuity measurement resolves them.

The page-89 rendering is retained in the parent project's
`analysis/mame-p6066/cpu19m-evidence/address-selection-page89.png`; extracted
supplement text is cached under `analysis/manuals/cpu19m/` with original PDF
page numbers. No PCB continuity or netlist claim is based on OCR alone.
