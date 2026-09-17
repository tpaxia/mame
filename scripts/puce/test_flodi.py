#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""Original-CAROM FLODI read oracle; no ROM or guest-memory injection.

The fixture is archive disk 121 (P6060 assembler). IMD parsing is confined to
this test: the emulated board consumes MAME drive flux. Also test a copy with
the first payload's IMD CRC-error flag set, leaving every payload byte intact.
"""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def track_zero(raw):
    pos = raw.index(b"\x1a") + 1
    mode, cyl, flags, count, size = raw[pos:pos + 5]
    assert (mode, cyl, flags, count, size) == (0, 0, 0, 26, 0), "Unexpected fixture geometry"
    pos += 5
    ids = raw[pos:pos + count]
    pos += count
    sectors = {}
    for sector in ids:
        location = pos
        kind = raw[pos]
        pos += 1
        assert 1 <= kind <= 8, "Unavailable fixture sector"
        length = 128 if kind & 1 else 1
        payload = raw[pos:pos + length]
        assert len(payload) == length
        pos += length
        sectors[sector] = (payload if kind & 1 else payload * 128, kind, location)
    return sectors


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--rompath", required=True, type=Path)
    ap.add_argument("--media", required=True, type=Path)
    ap.add_argument("--results", required=True, type=Path)
    ap.add_argument("--binary", type=Path, default=ROOT / "p6066")
    args = ap.parse_args()
    result = args.results.resolve()
    result.mkdir(parents=True, exist_ok=True)
    raw = args.media.read_bytes()
    sectors = track_zero(raw)
    assert sectors[26][1] in (3, 4), "Fixture must end in a deleted-data sector"
    assert all(sectors[s][1] <= 4 for s in range(5, 27)), "Fixture already has CRC errors"
    expected = b"".join(sectors[s][0] for s in range(5, 27))
    (result / "expected.bin").write_bytes(expected)
    damaged = bytearray(raw)
    damaged[sectors[5][2]] += 4
    bad_media = result / "data-crc-error.imd"
    bad_media.write_bytes(damaged)
    print("Source IMD SHA256", hashlib.sha256(raw).hexdigest())
    for name, media, status in (("good", args.media.resolve(), 0x80), ("bad-crc", bad_media, 0xc0)):
        env = dict(os.environ, P6066_FLODI_RESULTS=str(result),
                   P6066_FLODI_STATUS=str(status), P6066_FLODI_CASE=name)
        command = [str(args.binary.resolve()), "p6066", "-rompath", str(args.rompath.resolve()),
                   "-flop2", str(media), "-video", "none", "-sound", "none", "-nothrottle",
                   "-skip_gameinfo", "-seconds_to_run", "10", "-log", "-autoboot_delay", "0",
                   "-autoboot_script", str(ROOT / "scripts/puce/test_flodi_mame.lua"),
                   "-snapview", "Console", "-snapname", name, "-snapshot_directory", str(result)]
        run = subprocess.run(command, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True, timeout=90)
        (result / (name + ".log")).write_text(run.stdout)
        (result / (name + "-trace.log")).write_bytes((ROOT / "error.log").read_bytes())
        assert run.returncode == 0, run.stdout
        assert "PASS: FLODI" in run.stdout, run.stdout
        assert not any(s in run.stdout for s in ("LUA ERROR", "FAIL:", "Fatal error")), run.stdout
        assert (result / (name + "-payload.bin")).read_bytes() == expected
        print(run.stdout.strip())


if __name__ == "__main__":
    main()
