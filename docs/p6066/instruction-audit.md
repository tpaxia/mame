<!-- copyright-holders: Salvatore Paxia -->
# CPU19 / CPU19M instruction audit

Status: **canonical decode and execution-dispatch coverage complete; CPU audit
not signed off**. Do not use this result to declare the hardware complete or
resume speculative OS/FLODI debugging.

The independent source inventory in the parent P6066 research repository is
`analysis/mame-p6066/cpu-instruction-inventory.tsv`. Its acceptance script is
`analysis/mame-p6066/audit-cpu-instructions.py`. It enumerates canonical
encodings from the manual transcription, compiles the actual disassembler and
execution state, and checks both CPU variants. This is separate from semantic
tests: returning a mnemonic and accepting an opcode do not establish correctness.

## Sources and counting

* CPU19 Tabella Microistruzioni V2, PDF pp.1–13: 126 numbered entries,
  including ALFA and RESE (hardware-generated operations) and NOP (C900).
* V2 PDF p.14, also V1 PDF p.8: INCD0 and INCD1. V1 p.8 is rotated and
  its cached OCR contains virtually nothing; the scanned page has both entries.
* CPU19M supplement, 801.30.1 (03), printed pp.3.09–3.10, PDF pp.83/85 of
  `RA008 e ME006 - RAM per CPU19M - Descrizione di Funzionamento.pdf`:
  19 additional entries, visually transcribed from the detailed tables.
* CPU19 description 920.45.1: registers, byte/word addressing, memory
  handshakes, interrupt levels and software responsibilities.
* SUCE2 9756.30.1 (02), particularly PDF pp.14,17–18,29–47,83–91:
  console instructions, injected instructions, timing control, and GOTE
  channel test fixtures.

There are 147 source entries and 145 entries with memory encodings. These are
not 147 independent opcode families: conditional forms share families; NOP
is the zero-mask SEDI spelling; ALFA and RESE have no memory opcodes.
CPU19M is an explicit configuration option, enabled for the P6066 driver.

## Implemented in this audit

* Complete canonical disassembly, including service-console operations,
  word channel transfers, DEA and the CPU19M extensions.
* CPU19M arithmetic, complement/negation, clear, shifts, conditional +2,
  byte/word memory zeroing and the INTON/INTOF latch. External 3A/3B requests
  are masked by INTOF; level 1/2 polling remains enabled.
* Shared channel execution used by the live CPU and ROM-free tests:
  CAE, DEA, ESI/ESIM/ESIP, SEI/SEIM/SEIP, all console instructions and
  COM outputs, SADE input condition, plus DEL/EDAT/EDBT/EDTL.
* Word command and partial-data transaction interfaces preserve the lanes
  specified by the manuals. DEA does not invent a low output byte. The mask
  is an architectural validity mask, not proof of physical high impedance.
* Previously implemented channel instructions moved into the same tested
  execution path, so tests cannot accidentally exercise a substitute CPU.

## Semantics requiring particular care

| Operation | Audited interpretation |
|---|---|
| ADLL | Two ordered cross-half additions. Beta 2 sees beta 1's result for x=y. DI1 tests the final high byte, not the complete word. DI2 is the high-byte nibble carry. |
| SLL | Two ordered cross-half exchanges; x=y restores the original word. |
| Indexed memory and channel operations | Latch the old address, update the index, then transfer data. Short selectors 12–15 update A only. |
| DEA / DEL | Output Bx on ECD8–F with ECOT before sampling input; DEA loads Ax, DEL loads EPT:EPD into Lx. |
| EDAT / EDBT | Sample EPD and store the register before ECOT. |
| EDTL | Detailed p.3.10 gives EPT to Bx, EPD to Ax, without ECOT. The functional summary on p.3.11 conflicts with both the detailed text and its control word. Use the detailed table. |
| CAE | Whole-word command. The two beta phases present the same word; ECOT/ECOC accompany the second. The transaction adapter represents the completed strobe, not two commands. |
| ESI / SEI | Word addresses; ESI packs EPT as high byte, EPD as low byte. |
| EDC | Decrements only the low 12 bits, preserves DI, emits ECOF on zero. |
| COM11 / COM12 | CPU emits distinct CE21N/CE22N outputs. SUCE2's detailed START timing (PDF p.41) also assigns START suppression to COM11; do not implement the console using only the short CPU table description. |
| SADE | All sixteen values of the unused nibble decode identically; branch tests ECOFO and replaces the current counter's low byte. The physical source remains to be connected. |
| ALFA / RESE | Hardware-generated, not aliases for memory words F000/9000. 9000 is MEI M0; the old disassembler test expecting DW was wrong. |

## Validation

The following passed during this audit:

| Check | Result |
|---|---|
| Independent instruction inventory | 91,616 canonical encoding/variant pairs; no missing mnemonic or execution path |
| `test_register_matrix.py` | 32,251,904 CPU19 register cases |
| `test_branches.py` | 16,515,072 SAI/SAD/INCD cases; all levels, DI patterns, selector aliases and counter boundaries |
| `test_cpu.py` | 1,572,864 exhaustive ADD/SOT cases; reset, counters, flags, PC alias and internal-name regressions |
| `test_cpu19m.py` | 19,204,288 register/memory and interrupt-mask cases |
| `test_channel.py` | 1,137,484 channel cases, including ordering, all selectors, DI preservation and wrap boundaries |
| `test_byte_memory.py` | 15,360 indirect cases, direct addresses and lane preservation |
| `test_word_memory.py` | Word transfer, aliasing, access order and wrap fixtures |
| `test_disassembler.py` | 96 reference examples; length/fetch invariant over all 65,536 words |
| Bus and console suites | Existing arbitration and console tests pass |
| Original CAROM memory suite | RAM/ROM distinction, injected failure, all 32 RAM blocks and invalid-memory enumeration pass |
| MAME build | P6066 target builds |
| Bootstrap regression | 239 SIO attempts while HOME is busy; WAIT completes; sector 0/5 matches all 128 bytes at D700; missing-ME006 negative case passes |
| FLODI regression | 22 sectors / 2,816 bytes match; good and CRC-error media produce distinct expected status 80/C0 |

These counts are test cases, not a proof of all possible machine histories.

### Independent nanocode comparison

The parent research repository now contains
`analysis/mame-p6066/nanocode/`: a Python reference driven by the manually
transcribed CROM/TROM control words, with VROM recorded as timing annotations.
It compares actual C++ instruction handlers through an instrumented bridge;
the expected register and bus effects come from the Python datapath.

The comprehensive run passes 9,945,825 comparisons across all 145
memory-encoded instruction entries: 142 direct passes and three conditional
passes (INCD0/1 and INC21). Their printed control-table ambiguities are retained
in `source-issues.json`; enabling the documented hypotheses yields a consistent
logical control store. The literal-table run reports those discrepancies.
Eleven harness tests include deliberate corruptions of ALU controls, write
enables, phase ordering and selectors, which the comparison detects.

This independently corroborates the implemented instruction semantics under
the stated source interpretations. It does not certify the hardware-generated
ALFA/RESE paths, VROM timing, simultaneous interrupt requests or board wiring.

## Mandatory remaining sign-off items

1. **Interrupt acceptance and internal request arbitration.** The confirmed
   COM1 priority bug is fixed: BETA sets a saved pending request, and shared
   ALFA arbitration accepts it after INV/3A/3B (CPU19M pp.3.13–3.14). The parent
   project's `analysis/mame-p6066/interrupts/` tests the production loop:
   8,399 checks pass, including all original 11 failures and deferred service
   after each competing source. The transaction model retains unaccepted COM1
   until acceptance; exact latch retention/clear waveforms remain unverified.
2. **INOF reset, power fail, parity and invalid cycles.** INTON/INTOF and external
   masking exist, but power-fail/parity sources are not implemented. INV during
   interrupt service and invalid instruction fetch still stop. The INOF
   warm-reset behavior needs evidence; it is not covered by an opcode test.
3. **ECOFO / EC1F / EC2F wiring.** The CPU exposes SADE's input and COM/EDC
   outputs. The P6066 SADE source is deliberately unbound; it raises an explicit
   wiring error if used. Establish the latch relationships and level gating
   from CPU schematics/patent before connecting it to a convenient controller
   flag. COM5/6/9/10 peripheral effects need the same wiring audit.
4. **SUCE2 electrical inputs and injection.** Console instructions exist at the
   CPU boundary. The optional SUCE2 device is not implemented; absent output
   receivers consume no commands, while unbound input reads stop. SUCE2 PDF
   p.36 states that DIMEN inhibits ALFA's counter increment during injected
   instructions. The ordinary fetch path must not be reused unchanged for
   future SUCE injection.
5. **Per-operation timing and memory stalls.** Execution still uses provisional
   fetch/execute quanta. CPU19M's 275/225/200 ns improvements are differences,
   not a complete instruction timing table. The memory MEOC handshake and
   DMA/arbitration stalls need a separate timing implementation and tests.
6. **Noncanonical encodings.** ETIB's established low-nibble aliases remain
   supported. Other undocumented aliases need control-word/selector evidence;
   the canonical coverage test makes no claim about them.

Track hardware evidence gaps in the parent repository's `TBD.md`, not inside
MAME. Do not relabel these items as completed because another guest instruction
or another boot checkpoint happens to work.

## EDA/EDB aliases (2026-09-18)

Canonical A9x8/B8x8 input operations also accept low nibble9–F. V2 p.5 rows43–44
(CROM7FFF/BFFF, TROM1C/13) and US4032895 cols.13–14,20,31,33 establish that
RO0–2 affect no enabled path, while RO3 selects EPD. The CPU and disassembler
now use maskFF08 for these two families. This fixes the original066 startup
wordA97F without changing guest bytes. Channel tests cover every alias, register,
input byte, interrupt level and CPU variant, with one input read and no strobe.
The parent repository's nanocode/input-aliases.md records the circuit argument
and65,536 independent nanocode comparisons. This is not whole-ISA alias signoff.
