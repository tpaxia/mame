<!-- copyright-holders: Salvatore Paxia -->

# Word transfers and the first CAROM RAM test

Historical checkpoint: the subsequent [bootstrap increment](bootstrap-boundary.md)
passes BMI and reaches floppy-controller selection.

Implemented MLI, MLIM, MLIP, LMI, LMIM, LMIP and LPMIP, plus ICA, ICB, DCB and SLL. Cold CAROM now completes its repeated level-3 checks and the configured
64KB RAM scan and CAROM checksum. It stops before unsupported BMI (`89F4`) at
word `80AA`, with next PC `80AB`. This does not complete all CAROM self-tests.

## Transfer ordering

CPU19 *Tabella Microistruzioni V2*, PDF pages 7–8 (printed 2.103–2.104),
places address capture and index adjustment in the first phase, followed by
the data transfer. Thus an aliased store sees the updated index, and an
aliased load overwrites it. LPMIP adds one to the outgoing word without a
second source-register update. Word addresses are not divided by two.
Short indirect addresses use A12–A15; their adjustment preserves B.

The ROM-free `test_word_memory.py` covers 15 transfer fixtures, short/full
wraparound, untouched state, invalid instructions, and the active-PC alias
at CAROM's MLIP M1,L1. Byte increment/decrement flag checks are included.
The disassembler now has 76 reference cases.

## Investigation of the apparent level-4 discrepancy

An initial reading incorrectly labelled the branch to `80B7` as a memory
failure. Executing both successful and deliberately faulty memory tests
shows that it is the writable-memory path.

At `8099`–`809D`, CAROM writes and reads `00FF`, then `FF00`, at the same
word address. It swaps L6 before comparing opposite register halves. With
working RAM, A6=00 and B9=FF: XOR is FF, D1 is clear, and `80A2` branches to
`80B7`. This difference establishes that the contents changed. If writes
are ignored and both reads return the same value, both comparisons are zero
and execution instead reaches `80BA`, which performs a separate checksum
routine. The broader interpretation of that alternate path as a ROM probe
is an inference; it is not needed to establish the RAM result.

`80B7` loads return value AC80 and enters the RAM pattern routine at `818B`.
The routine sets D4 (success), stores and verifies patterns, and uses D6 to
select two pattern variants. Its actual comparison-error branch is `81B6`;
that path clears D4 at `81BD`. Both success and failure reach the common
exit at `81BE`. Reaching that address alone cannot diagnose a memory fault.

The original-ROM regression uses the live CPU state implementation and
injects faults only through its memory callbacks:

| Memory behavior | Observed result |
|---|---|
| Writable RAM | 2,048 pattern stores and 2,048 verification reads; no visit to 81B6; D4 remains set at 81BE |
| Writes ignored | Alternate probe path at 80BA, with return value A880 |
| First verification read corrupted by one bit | Branch to 81B6, D4 cleared, common exit at 81BE |

These counts cover two passes over 1,024 words, not the whole RAM map.
The MAME cold run before implementing SLL agreed: level 4, L1=807D, L2=AC80, DI=97 (D4 set),
ECORN released, 256 lamp strobes and lamp register FFFF. The integration
fixture now explicitly asserts D4 rather than treating the exit address as
proof of success. Console output and panel restart also pass.

## Reproduce

```sh
python3 scripts/puce/test_word_memory.py
python3 scripts/puce/test_carom_memory.py --carom /path/to/carom.bin
```

The latter verifies the reference ROM SHA-1 before use. No ROM bytes are
included. Full MAME instructions and console checks are in [console.md](console.md).

## SLL and the full configured RAM scan

V2 PDF p.12 (printed 2.108) defines SLL as two ordered cross-half exchanges:
A(x) with B(y), then A(y) with B(x). The core preserves DI and follows that
order even when the operands coincide. The regression checks distinct
operands, the identical-register case (two exchanges cancel), and a live-PC
return. At 81BF, SLL L0,L2 converts saved AC80 into PC 80AC and saves the
advanced return counter as C081.

The original-ROM test now also continues through all 32 RAM blocks, counting
65,536 pattern stores and 65,536 verification reads with no error-branch
visits. It then executes the checksum routine over CAROM and reaches BMI at
80AA with L4=8000 and D4 still set. This validates the configured map, not a
physical memory-board implementation. Actual MAME independently reaches that
checkpoint and passes the D4, console-output and restart assertions.

The next unsupported operation is BMI. Further memory enumeration beyond
this checkpoint, interrupts, bus arbitration and bootstrap remain open. Provisional input defaults,
memory population and timing assumptions are unchanged.
