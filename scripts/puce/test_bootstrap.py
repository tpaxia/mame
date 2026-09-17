#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""Verify disk-121 CAROM loading and level-3 entry with/without ME006."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
FIXTURE_SHA256 = "9208fda4c8ecc74cba0633c1a38380e5412e79a85b5a73b6e151e8be4f257776"


def sectors(raw):
    """Independent IMD oracle, never used by the emulated controller."""
    pos = raw.index(b"\x1a") + 1
    result = {}
    while pos < len(raw):
        mode, cyl, head, count, size = raw[pos:pos + 5]
        assert mode == 0 and head == 0 and count == 26 and size == 0
        pos += 5
        ids = raw[pos:pos + count]
        pos += count
        for sid in ids:
            kind = raw[pos]
            pos += 1
            assert 1 <= kind <= 4
            length = 128 if kind & 1 else 1
            value = raw[pos:pos + length]
            assert len(value) == length
            result[cyl, sid] = value if kind & 1 else value * 128
            pos += length
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--rompath", required=True, type=Path)
    ap.add_argument("--media", required=True, type=Path)
    ap.add_argument("--results", required=True, type=Path)
    args = ap.parse_args()
    result = args.results.resolve()
    result.mkdir(parents=True, exist_ok=True)
    raw = args.media.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == FIXTURE_SHA256, "Not the validated disk-121 fixture"
    disk = sectors(raw)
    descriptor = disk[1, 1]
    manifest = []
    for off in range(0, 40, 8):
        record = descriptor[off:off + 8]
        if record[0] != ord("D"):
            continue
        count = record[1]
        address = int.from_bytes(record[2:4], "big")
        cyl, sid = int(record[4:6]), int(record[6:8])
        expected = b"".join(disk[cyl + (sid - 1 + i) // 26, (sid - 1 + i) % 26 + 1] for i in range(count))
        (result / f"expected-{address:04x}.bin").write_bytes(expected)
        manifest.append(f"{address:04x} {len(expected)}\n")
    assert descriptor[40:44] == b"F\x00\x10\x00"
    (result / "blocks.txt").write_text("".join(manifest))
    for case, slot in (("installed", []), ("removed", ["-bus:microcode", ""])):
        env = dict(os.environ, P6066_BOOT_RESULTS=str(result), P6066_BOOT_CASE=case)
        command = [str(ROOT / "p6066"), "p6066", *slot, "-rompath", str(args.rompath.resolve()),
                   "-flop2", str(args.media.resolve()), "-video", "none", "-sound", "none",
                   "-nothrottle", "-skip_gameinfo", "-seconds_to_run", "15", "-log",
                   "-autoboot_delay", "0", "-autoboot_script", str(ROOT / "scripts/puce/test_bootstrap_mame.lua"),
                   "-snapview", "Console", "-snapname", case, "-snapshot_directory", str(result)]
        run = subprocess.run(command, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True, timeout=90)
        (result / f"{case}.log").write_text(run.stdout)
        (result / f"{case}-trace.log").write_bytes((ROOT / "error.log").read_bytes())
        assert run.returncode == 0 and "PASS: bootstrap" in run.stdout, run.stdout
        assert not any(s in run.stdout for s in ("LUA ERROR", "FAIL:", "Fatal error")), run.stdout
        assert run.stdout.count("PASS: bootstrap") == 1, run.stdout
        if case == "removed":
            assert "unclaimed write at word A000" in (result / f"{case}-trace.log").read_text()
        assert (result / f"{case}.png").is_file(), "Missing console snapshot"
        print(run.stdout.strip())


if __name__ == "__main__":
    main()
