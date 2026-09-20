# Discard-output printer

<!-- copyright-holders: Salvatore Paxia -->

The user explicitly requested a printer that completes requests while discarding
output. GOINO's previous VIASN/FAINN/FINTN/FISTN no-ops could leave ESE waiting
forever; accepting a command was not equivalent to completing it.

The functional stub now supplies asynchronous printer events through the normal
GOINO source latches, synchronization, arbitration and level2/3 ownership:

- VIASN starts with three blank columns (the manual specifies fewer than seven),
  then matrix requests alternating with seven discarded columns per matrix.
- REMAN acknowledges matrix/line-feed events.
- FISTN stops column generation and schedules a functional carriage-return
  completion event, including when used to prepare a new line.
- FAINN supplies line-feed events until FINTN stops them. ESE decides the count.
- Events use the existing6.3ms scheduler, independently of the CPU timer-enable
  command. This is functional pacing, not printer speed emulation. An outstanding
  request/grant is serviced and cleared before another event is emitted.

No printer file, rendered paper, motor, thermal head or consumable is modeled.
The previous user-approved inert printer/decimal status byte remains0. Counts,
phase and remaining columns are saved with GOINO state. Diagnostic log lines
report start/end commands and cumulative discarded columns/feed events.

The native common IRQ prologue reads EPD at A125 without a preceding DEA.
F27E can leave the input mux at3, which used to terminate as a missing PROM.
The discard printer now supplies inert0 only for that mux during exclusively
printer-owned level3 service. This is a stub policy, not recovered PROM data;
deliberate direct PROM reads still fail explicitly.

Sources: GOINO description printed7–8 (PDF11–12), printed11/15–17
(PDF15/19–21), including figures1.4/1.6. Matrix/column ratio and handshake
sequence are documented; discard policy, timing and carriage completion are
functional abstractions. User authorization supersedes the former no-op policy.

Validation: production CPU/bus/GOINO integration exercises return completion,
initial columns, matrix acknowledgement, seven data columns and ten feed events,
then verifies no remaining printer IRQ. State-level tests and all existing
GOINO transport/event tests pass. Live native CATALOG exercised line-feed
completion; the GTL3.2 program `10 PRINT 12345 / 20 END` was RUN twice with
NO PRINT off and returned to READY each time. Native PRINT ALL/CATALOG ran
110 seconds without a crash. Live logs did not establish nonzero column output
for those configured images; the actual column path is covered by the production
integration fixture, not claimed as observed guest output.
