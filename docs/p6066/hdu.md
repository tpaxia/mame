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
sectors with FF, and refuses to overwrite files. No physical formatting is
required. For the recovered 064/068 generation procedure, add `--ese-geometry`
to seed the ten geometry bytes at CY0/ST7 before generation. This is the
explicitly authorized preparation workaround, not a complete Olivetti label.
Logical initialization and installation of ESE are still necessary.

Add to a normal MAME launch:

```
-bus:dma rodma -bus:hdu difo -hard1 /path/to/disk.chd
```

Use `-hard2` for the second drive. MAME's standard hard-disk image device handles
CHD storage and differencing images. Incorrect geometry and raw images are
rejected. The generated 064/068 installation with geometry prepared before
generation has cold-booted to READY; see outer `analysis/hardware/hdu/boot-chd`.

## Healthy-medium abstraction

DIFO still implements commands, DMA, interrupts and provisional mechanical
timing. The drive derives CY/ST IDs from the current cylinder and head/slot:
ST = head × 48 + slot. The 49th rotational slot has no normal sector ID and
stores no CHD payload. Both service cylinders remain addressable.

Successful reads supply good CRC status. Host read/write failures propagate.
Ordinary format commands with canonical IDs fill sectors with FF; raw CRC,
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
