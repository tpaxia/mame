<!-- copyright-holders: Salvatore Paxia -->

# P6066 configurable backplane

The development machine has populated, non-fixed MAME slots. Defaults are
installed automatically; users can remove cards or choose another implemented
option through MAME's **Slot Devices** menu or command-line slot options.
Slot configuration changes require restarting the machine, not live insertion.

| Slot | Default | Other implemented options |
|---|---|---|
| `bus:ram0` through `bus:ram3` | `ram16` (16 KB each) | `ram8`, `romca`, empty |
| `bus:rom` | `romca` | `ram16`, `ram8`, empty |
| `bus:console` | `goino` | empty |
| `bus:floppy` | `flodi` | empty |
| `bus:microcode` | `me006` (32 KB physical, 28 KB visible by default) | empty |

The memory slot defaults place RAM at word addresses 0000, 2000, 4000 and
6000, and CAROM at 8000. Each memory card exposes an address jumper setting
in Machine Configuration; the initial setting uses its slot's default address.
Overlapping memory decode is reported as an error. Removing a required board
can make firmware fail or prevent boot: an empty slot does not supply hidden RAM,
ROM or controller responses.

Examples, adding the ROM path appropriate to your installation:

```sh
# Inspect compatible board and drive options.
./p6066 p6066 -listslots

# Remove the fourth lower-RAM board: 48 KB lower RAM remains.
./p6066 p6066 -bus:ram3 ""

# Replace the fourth lower-RAM board: 56 KB lower RAM remains.
./p6066 p6066 -bus:ram3 ram8

# Remove FLODI and reproduce the no-controller bootstrap timeout.
./p6066 p6066 -bus:floppy ""

# Remove the console board.
./p6066 p6066 -bus:console ""
```

FLODI also owns two configurable drive connectors,
`bus:floppy:flodi:0` and `bus:floppy:flodi:1`, each defaulting to `8sssd`.
Removing the controller removes those connectors. Drives and mounted media are
separate: an installed empty drive is not a missing controller.

The bus owns memory decode, peripheral selection, reset distribution and
interrupt ownership. Acknowledgement latches the interrupting card until the
CPU ends that service level, so later selections or withdrawn requests cannot
redirect the active channel. The functional arbiter prioritizes levels 1, 2,
3A and 3B; slot position breaks ties. Physical connector numbering, electrical
timing and chassis priority order remain unverified. DMA bus ownership is not
implemented yet.

RAM capacities and slot labels describe the development configuration, not a
verified historical chassis inventory. Serial and additional board options will
be added as their devices are implemented. FLODI completes the original CAROM
22-sector read on the disk-121 fixture, with guest-RAM and CRC-status checks.
See [FLODI evidence](flodi-evidence.md). ME006 supplies words 8800–BFFF
with its 8000 window and first 2 Kwords excluded; Machine Configuration exposes
the documented four windows and initial-bank exclusions. Remove it with
`-bus:microcode ""`. The default has 64 KB lower RAM plus 28 KB visible ME006.
Original CAROM now loads all four disk-121 blocks and enters disk firmware.
See [ME006 evidence and bootstrap validation](me006-bootstrap.md). Console
initialization remains incomplete; this is not yet a usable operating system.

## Validation

`python3 scripts/puce/test_bus.py` tests interrupt priority, nesting and ownership.
`scripts/puce/test_slots.sh ROM_DIRECTORY RESULTS_DIRECTORY` runs original CAROM
with FLODI removed in four configurations: default RAM, fourth RAM board removed,
fourth RAM board replaced with 8 KB, and console removed. It verifies discovered
memory size, invalid-memory cycles, and arrival at the controller timeout.
