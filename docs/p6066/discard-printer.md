# Discard-output printer

<!-- copyright-holders: Salvatore Paxia -->

The user requested a printer that completes requests while discarding output.
This is a functional ESE-facing stub, not a faithful ASTAM mechanical model.
It uses normal GOINO request latches, synchronization, arbitration, and level2/3
ownership. No paper, output file, motor or thermal head is rendered.

The integrated printer connector under GOINO is empty by default. Attach this
functional model at startup with `-bus:console:goino:options printer`. The
printer was optional in the P6066 General Manual, printed p.1-25 (PDF p.39).
With the connector empty, printer commands are accepted by GOINO but cannot
produce printer requests. Selector 1 reports printer absent (`40`), rather than
the attached printer's idle status (`00`). This does not by itself establish a
printer-free installed-system configuration.

## Current functional contract

- Command F starts column transfer: three leading blank columns, then matrix
  requests alternating with seven columns per matrix.
- Command 1 ends column transfer. It does not create an extra completion IRQ.
- Command 2 produces feed interrupts until command 3 stops them.
- Command 4 acknowledges matrix/feed requests. Level2 ECOT clears column requests.
- The existing6.3ms scheduler supplies functional pacing, independently of TIMEN.
  A pending request/grant is serviced before another event is generated.
- With the printer attached, status selector 1 returns bit 4 (`10`) while
  printing or feeding, zero when idle. With the slot empty, it returns bit 6
  (`40`) for printer absent. The low nibble independently carries the decimal
  wheel position, including when the printer is absent.
- A stale selector3 during owned level3 GOINO service returns inert0, including
  keyboard/timer overlap. Direct level4 PROM access still fails without a dump.

## Evidence and unresolved conflict

The GOINO description, figure1.3 (PDF9, printed5), explicitly labels command1
VIASN/start and commandF FISTN/end. Printed7–8 and16–18 describe printing,
seven columns per matrix, feed completion, and STOCN busy conditions. Those
labels are **opposite to the transfer phases observed in GTL3.2 firmware**.
The stub deliberately follows the observed software contract. This is not a
claim that the manual is wrong or that the physical command decoding is solved.
The prior implementation followed those labels, then added an invented return
completion IRQ; that combination did not implement the firmware's transfer.

In SYSTEM_DISK_R_3_2_GTL3, PUCE BE08–BE68 probes status with DEA90. The SDIA/SDIB
exchanges matter: status bit4 branches to BE5C and returns a busy/retry result.
Bit3 tested later belongs to saved software flags, not this hardware busy bit.
BE38 emits FF after installing the matrix count/buffer. A403 acknowledges each
matrix; A427 decrements the remaining count and A42E–A444 prepares its buffer.
After the count reaches zero, A418 emits F1, and the state changes to feeding.
A410 emits F2; A447 emits F3 when the feed count is exhausted.

The earlier constant-zero status admitted new requests during an existing feed,
overwriting its software state. The old F1-start/F-stop model then generated
repeating start/feed cycles or stalled after a single fabricated completion.
Busy bit4 and the observed transfer direction are both necessary corrections.

A124 EDB (next PC A125) reads the previously selected input before the shared
GOINO interrupt dispatch. An Fxxx output can leave selector3 selected even for
a keyboard interrupt. Restricting the inert read to printer-only source bits
caused the earlier PROM exception. The level3 fallback is explicit stub policy;
real PROM access during such service is not implemented or distinguished here.

## Validation

Production CPU/bus/GOINO integration checks active/idle status, column and matrix
handshakes, feeding, nested service, incidental selector3 reads on a timer IRQ,
and rejection of a direct level4 PROM read. Event and transport suites pass.

The live regression sends CATALOG three times, then enters NEW / 10 DISP 12345 /
20 END and RUN twice, without pressing NO PRINT. All three catalogue transfers
finish (3,216 discarded columns,300 feed events); both BASIC runs display12345
and return READY. Final CPU is running at level4, with no subsequent printer
commands. Observer, transcript and reproduction notes are in the outer repo at
`analysis/mame-p6066/discard-printer/catalog-regression/`.

The earlier PRINT-only test did not observe column transfer and was insufficient
to establish CATALOG completion. Physical timing, status wiring, the command
polarity conflict, and PROM contents remain unresolved.

The P6066 General Manual, printed pp.5-180–5-181 (PDF pp.432–433), says that
`PRINT` displays its text on the integrated display and waits for `CONTINUE`
when the installed system configuration has neither integrated nor auxiliary
printer. Printed pp.3-25–3-26 (PDF pp.91–92) say `CONFIGURE` settings are
stored on the system disk and omitting `EP` selects the integrated printer;
physical detachment alone therefore does not establish a printer-free software
configuration. The GOINO description, printed p.7 (PDF p.11) and printed p.18
(PDF p.22), says the CPU probes `ALBAA`, `STOCN`, and `OPTON` before printing;
`OPTON` indicates absence. The GTL3.2 printer probe at BE09–BE10 tests input
bit 6 on the absent branch, bit 4 on the busy branch, and bit 5 on the other
printer-condition branch. The empty slot now supplies bit 6 as a functional
`OPTON` model; the exact DISL006 electrical mapping remains unverified. The
documented `PRINT`/`CONTINUE` fallback still needs a verified printer-free
configured system disk and independent integration coverage.
