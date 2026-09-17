<!-- copyright-holders: Salvatore Paxia -->

# ADD/SOT arithmetic

The CPU now implements ADD, ADDA, ADDB, SOT, SOTA and SOTB. Cold CAROM passes
its arithmetic comparison at 804B–804D and stops before MLIP M1,L1 (`DE11`) at
word **804E**, with next PC 804F. The program has not completed self-test.

## Semantics

The source is CPU19 *Tabella Microistruzioni V2*, PDF p.1 (ADD family) and
p.12 (SOT family). Encodings are 86xy/96xy/A6xy and B6xy/C6xy/D6xy.

ADD computes `A(x) + B(y) + old DI0`. SOT computes
`A(x) + complement8(B(y)) + old DI0`, equivalent to `A(x) - B(y) - !old DI0`.
Thus DI0 is carry/no-borrow; a zero DI0 supplies an incoming borrow for SOT.
Both operands and incoming DI0 are sampled before destination/flag updates.

| Flag | Updated meaning |
|---|---|
| DI0 | Carry out of the 8-bit addition; no borrow for subtraction |
| DI1 | The 8-bit result is zero, including a wrapped result |
| DI2 | Carry from the low nibble; no low-nibble borrow for subtraction |
| DI3–DI7 | Preserved |

DI2 is not signed overflow. ADD and SOT discard the result but update flags.
The A-suffixed forms write A(x), preserving B(x); the B-suffixed forms write
B(y), preserving A(y). Operands may be halves of the same scratchpad register
or may alias an active microprogram counter. Fetch has already advanced that
counter when the arithmetic instruction reads it.

## Checks

The existing `test_cpu.py` now exhausts all 256-by-256 operand pairs, both
incoming carry values, six operations, and separate/aliased register pairs:
**1,572,864 cases**. Its independent subtraction oracle uses subtraction and
borrow comparisons rather than the implementation's complemented addition.
The tests check destinations, untouched registers, all flag results and
preservation of upper DI bits. A boundary test exercises an active PC operand
after fetch crosses a page. No original ROM is required for these tests.

The decoder has 62 reference cases plus exhaustive single-word fetch/length
checks. The optional original-CAROM state fixture remains stopped at ENTL
because it does not implement external input callbacks; the actual MAME
integration test covers the longer cold path.

The MAME integration test verifies A0=92 and DI1=1 at the new MLIP stop,
ECORN still low, 256 lamp strobes, synthetic console/display output and panel
restart. All three PASS markers must be present. The earlier reset/input
assumptions still apply: undriven type/data inputs are provisionally logical
zero and are not evidence of completed peripheral emulation.

## Next step

Implement word-memory transfers and their register-update ordering. The next
CAROM instruction, MLIP M1,L1, uses the active program counter as both address
register and destination. Later LMIP/LMIM instructions deliberately alias the
address and source registers. Their side-effect order must be established from
the manual and tested rather than inferred from conventional CPU behavior.

The all-on console lamp pattern remains unchanged at the current stopping
point. No new display message has been emitted by CAROM.
