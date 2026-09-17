<!-- copyright-holders: Salvatore Paxia -->

# CAROM to the floppy bootstrap boundary

The original CAROM now runs from reset through the register/ALU checks,
64KB RAM pattern tests, its ROM checksum, memory enumeration, and floppy
bootstrap initialization. It selects peripheral E0 at 8308. With no floppy
controller attached, firmware takes its timeout path, sends lamp word 8084,
and loops at 80B6. There is no unsupported CPU instruction on this path.
This is bootstrap **entry**, not a disk read or completed system boot.

## Batched CPU work

Implemented AMD/MAD and the complete AMI/BMI/MAI/MBI indirect byte-transfer
families, including increment/decrement forms. The live CPU state helper
owns address capture, update ordering and register halves; the MAME wrapper
converts byte addresses into word addresses and lane masks. Stores preserve
the other byte. Index adjustment precedes the data phase, including aliases.
AZAM/AZAP/AZBM/AZBP clear their documented nibbles without modifying DI.

Sources: CPU19 *Tabella Microistruzioni V2*, PDF pp.2–3 and 7–8, printed
2.098–2.099 and 2.103–2.104. The decoder covers these families and has 95
reference cases, plus exhaustive single-word fetch/length checks.

## Invalid memory cycles

The initial machine map incorrectly treated unpopulated space as readable
zero-filled memory. CAROM consequently entered its checksum failure path.
The ROMCA manual (STAC 670.30.1, PDF p.67, printed ROMCA 5, figures 3–4)
explicitly describes INV00 and MEOCN: each responding board contributes
address validity; when the cycle completes and no board claims the address,
the CPU receives an invalid-address indication. This is not simply a memory
timeout. ROM writes remain valid, ignored cycles.

The driver now signals invalid accesses for word addresses 8800–FFFF. The
CPU defers service until the current data-transfer instruction completes,
then enters level 3 through the existing L1. CAROM's handler at 807D uses D3
to distinguish its probe and adjusts the saved level-4 continuation itself.
No firmware addresses are patched or special-cased in the implementation.

**Evidence boundary:** the invalid-address signal is directly documented.
Its mapping to the synchronous level-3 request and the chosen completion
ordering are inferred from the CAROM handler and the documented internal
level-3 request mechanism, and are validated against the original firmware.
They still need circuit-level confirmation. The arbitrary zero data on an
invalid read, cycle timing, invalid instruction fetches, and invalid accesses
while already at levels 1–3 are not established hardware behavior. The latter
two cases fail explicitly rather than silently execute fabricated data.
This model does not implement external EPR arbitration, DMA or board slots.
Debugger reads do not assert the invalid-address indication.

## Evidence at the boundary

The original-ROM regression exercises writable memory, ignored writes, and
an injected bad read. It also continues through all 32 RAM blocks and then
through the complete address scan:

- 65,536 pattern writes and 65,536 verification reads; no RAM-error branch.
- 30 unpopulated 1,024-word blocks invoke the probe recovery handler.
- RAM extent byte 0E becomes 80; bootstrap words 2/3 contain 4000/1000.
- L1 becomes 8226 and CAROM reaches selection of E0 at 8308.

Actual MAME independently verifies the 30 invalid cycles, one E0 selection,
bootstrap parameters, firmware timeout loop, and lamp word 8084. The console
regression also checks BMI even/odd lanes and short addressing, synthetic
lamp/display output, and a full restart through the same firmware path.
A diagnostic output `floppy_selects` counts E0 selections; it is not a hardware
lamp or a substitute for a controller response.

```sh
python3 scripts/puce/test_byte_memory.py
python3 scripts/puce/test_carom_memory.py --carom /path/to/carom.bin
```

The byte test covers 15,360 indirect cases, all 256 direct addresses, lane
preservation and nibble clears. MAME invocation is in [console.md](console.md).
No original ROM bytes are distributed.

## Next milestone

Implement the floppy controller and its selection/status/interrupt protocol,
then let CAROM read a real boot sector. This requires external interrupt
ownership and acknowledgement, not a forced jump past the timeout. The
functional display renderer is present; keyboard/buttons, bus slots, DMA,
serial boards and disk-loaded ESE remain later gates. CPU timing and memory
population remain provisional.
