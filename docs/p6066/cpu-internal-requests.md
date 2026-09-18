<!-- copyright-holders: Salvatore Paxia -->

# CPU internal request names and busy-SIO regression

CPU19M publication 801.30.1 (03), printed pp.3.13-3.14, documents the
internal/external priority decoder. The local scan is the CPU supplement
inside `RA008 e ME006 - RAM per CPU19M - Descrizione di Funzionamento.pdf`,
PDF pp.91/93. DISL600 M3 and DISL602 A7-G7 give COM1/ICOON name 02 and
invalid-memory/ICMEN name 03. External requests have EX01 clear.

The previous core entered level 3 on COM1 but returned the undriven name 00.
Disk-121 firmware therefore ran its GOINO service instead of its CPU service.
A read SIO issued while HOME was active took the documented-in-code busy
retry path but returned with DI6 still set, switched into an empty PSW and
executed an invalid instruction at address zero. A0F9 was an error loop,
not an I/O wait. HOME's later command 00 did not cause this earlier failure.

The CPU now retains the internal name through level-1/2 preemption, exposes
it to name-input instructions at level 3, and clears it on level-3 COM0.
The field participates in save states. Cold-reset name defaults are unchanged;
complete simultaneous-source arbitration and parity/power-fail handling are
not established by this change.

The original supervisor now dispatches to its CPU routine and normalizes the
saved DI. SIO at software byte 2186 retries 239 times while HOME is busy;
it then starts the read and WAIT at 218E completes. The new `software-read`
checkpoint independently compares cylinder 0, sector 5 with all 128 bytes
at D700 and verifies the controller count increased from 206 to 207 sectors.
CAROM's 23,424 loaded bytes and the absent-ME006 negative case also pass.
No guest bytes, registers or FLODI statuses were patched.

```
python3 scripts/puce/test_cpu.py
python3 scripts/puce/test_bootstrap.py --rompath ../analysis/mame-p6066/roms --media ../analysis/mame-p6066/media/p6060-assembler-121.imd --results ../analysis/mame-p6066/cpu-name-software-read --checkpoint software-read
```

The obsolete `firmware-dispatch` checkpoint was removed because it accepted
an instruction reached after the erroneous context switch. ETIB alias tests
remain valid. A longer exploratory run reaches DEA (FB78) at word BE09,
which is not yet implemented; this is not complete ESE startup. The parent
project's `analysis/mame-p6066/disk121-busy-context.md` contains the detailed
before/after trace and the still-open standalone HOME timing question.
