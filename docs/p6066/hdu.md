# P6066 HDU2110 and DIFO

<!-- copyright-holders: Salvatore Paxia -->

The optional HD configuration adds DIFO and the separate RODMA shared-memory
bridge, with two HDU2110 image connectors. Neither board is fitted by default.

## CHD images

Use MAME CHD with **202 cylinders, 4 heads, 48 sectors per head, 256 bytes per
sector**. This includes two service cylinders: 9,928,704 bytes total, of which
9,830,400 bytes are in the 200 user cylinders (marketed as 10 MB).

From the outer project:

```
python3 tools/p6066_hdu_image.py create /path/to/disk.chd
python3 tools/p6066_hdu_image.py info /path/to/disk.chd
```

The tool uses the local `mame/chdman`, creates uncompressed writable CHD, fills
sectors with FF, and refuses to overwrite files. CHD does not store magnetic
sector markers. For original preparation, create a blank image without
`--ese-geometry`, run HDI E9 from the recovered utilities floppy, then run DKS
with generator064 and master068 for logical initialization and installation.
HDI writes geometry, ERMAP and service/check records in the sector payloads.
The historical `--ese-geometry` option seeds only eleven startup bytes and
is retained for earlier experiments; it is not complete HDI preparation.
See outer `analysis/hardware/hdu/README.md` for the verified procedure/evidence.

Add to a normal MAME launch:

```
-bus:dma rodma -bus:hdu difo -hard1 /path/to/disk.chd
```

Use `-hard2` for the second drive. MAME's standard hard-disk image device handles
CHD storage and differencing images. Incorrect geometry and raw images are
rejected. Original HDI followed by064/068 DKS has generated a pair that cold-boots to
READY in current MAME with PR 6610 attached, without host input. Use reset
BASIC mode, unshifted alphabetic keys and main END OF LINE for DKS; see outer
`analysis/hardware/hdu/install-current-keyboard` and `boot-current-keyboard`.

## Healthy-medium abstraction

DIFO still implements commands, DMA, interrupts and provisional mechanical
timing. The drive derives CY/ST IDs from the current cylinder and head/slot:
ST = head × 48 + slot. The 49th rotational slot has no normal sector ID and
stores no CHD payload. Both service cylinders remain addressable.

Successful reads supply good CRC status. Host read/write failures propagate.
Format commands with canonical IDs fill sectors with FF, including HDI's
ID-only template (data marker FF) followed by ordinary data writes. Data
markers are implicit in this abstraction. Raw CRC,
relocated IDs, defective sectors and incomplete-format states are not stored.
Malformed/noncanonical format templates are rejected. Controller transfer
errors are still reported, but an interrupted write does not leave a persistent
bad-CRC flag in the image. This is an intentional healthy-disk abstraction,
not a magnetic recording model or a guarantee that arbitrary guest software
will work. No image/save-state rollback contract is claimed.

## Migrating legacy images

The former `.phd` runtime container is no longer supported. Preserve the source
and convert a healthy, canonically formatted image:

```
python3 tools/p6066_hdu_image.py convert old.phd disk.chd
```

Every normal sector payload, including service cylinders, is copied in CHS
order. Missing/bad/noncanonical sectors or used spare slots are rejected rather
than silently discarded. Supplied but unvalidated raw ID CRC bytes are omitted.
Historical analysis directories retain their original `.phd` artifacts.

## Synthetic MAME integration test

Create a disposable blank image with the outer tool, then run from `mame`:

```
./p6066 p6066 -bus:dma rodma -bus:hdu difo \
  -bus:console "" -bus:floppy "" -hard1 /tmp/hdu-test.chd \
  -rompath ../analysis/mame-p6066/roms -video none -sound none \
  -nothrottle -skip_gameinfo -seconds_to_run 5 -autoboot_delay 0 \
  -autoboot_script scripts/puce/test_hdu_mame.lua
```

The fixture writes synthetic PUCE into RAM; it does not modify CAROM or any ESE
software. It formats a track and tests write/read/scan/verify through the actual
CPU, bus, timers, shared RAM and completion interrupt. Successful execution
prints three `PASS:` lines and exits. Check that there are no Lua assertions or
MAME exceptions; process exit status alone does not establish a passing test.

## Functional activity indicators

Both console layouts display FD1, FD2, HD and SHD activity. Green means
read/search (including verify/scan), red write/format/erase, amber seek, and
dark idle. These are emulator indicators, not claims about original lamp
wiring. Controller state is sampled every1ms with a100ms release hold;
absent boards remain dark. No disk status or guest timing depends on them.

`test_disk_activity_mame.lua`, run on disposable generated two-HD media with
SYSBTS in FD2 and FD1 empty, checks independent read indications, HD write,
empty-drive isolation and expiry after startup. The real-boot test passed.
