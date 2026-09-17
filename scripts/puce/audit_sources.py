#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders:P6066 contributors
"""Inventory local P6066 evidence; never copy or alter ROM/media contents.

Usage: python3 scripts/puce/audit_sources.py --project .. --output ../analysis/mame-p6066
Checksums identify inputs; files named after CRCs are not assumed to be
individual physical ROM chips. Mapping and interleave require separate proof.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[2]


def describe(project, path):
    data = path.read_bytes()
    return {'path': path.relative_to(project).as_posix(), 'bytes': len(data),
            'crc32': f'{zlib.crc32(data):08x}',
            'sha1': hashlib.sha1(data).hexdigest(),
            'sha256': hashlib.sha256(data).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    project = args.project.resolve()
    paths = set()
    for folder in ('AndreaMaglio/DocumentazioneP6060_P6066/ROM DUMP',
                   'AndreaMaglio/DocumentazioneP6060_P6066/Reverse engineering schede/GISA2',
                   'upstream-olivetti-p6060/rom/system'):
        paths.update((project / folder).rglob('*.bin'))
    for name in ('068.IMD', 'K0E002.bin', 'K0E003.bin', 'K0E002-load-0180.bin',
                 'K0E002-load-1000.bin', 'K0E002-load-8800.bin'):
        paths.add(project / 'analysis/master-ese' / name)
    files = [describe(project, p) for p in sorted(paths)]
    by_hash = {}
    for item in files:
        by_hash.setdefault(item['sha256'], []).append(item['path'])
    service = project / 'AndreaMaglio/DocumentazioneP6060_P6066/Manuali STAC e di servizio/2L'
    required = ['CPU19 - Descrizione (Aggiornamento Tecnico).pdf',
                'CPU19 - Tabella Microistruzioni V1.pdf',
                'CPU19 - Tabella Microistruzioni V2.pdf',
                'RODMA e DMARO - ROM con DMA - Descrizione di Funzionamento.pdf',
                'FLODI - Governo Floppy - Descrizione di Funzionamento.pdf',
                'GOINO e ASTAM e CONDY e ROMCA e TASTIERA - Descrizione di Funzionamento.pdf']
    sources = [describe(project, service / name) for name in required]
    result = {'mame_commit': subprocess.check_output(
                  ['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
              'mame_branch': subprocess.check_output(
                  ['git', '-C', str(ROOT), 'branch', '--show-current'], text=True).strip(),
              'files': files, 'manuals': sources,
              'identical_files': [v for v in by_hash.values() if len(v) > 1],
              'physical_rom_mapping': 'UNVERIFIED: sizes/hashes do not establish chip organization',
              'initial_boot_disk': 'UNSELECTED: master 068 is generation material'}
    args.output.mkdir(parents=True, exist_ok=True)
    target = args.output / 'inventory.json'
    target.write_text(json.dumps(result, indent=2) + '\n')
    print(f'{len(files)} ROM/media files and {len(sources)} manuals: {target}')
    print(f'{len(result["identical_files"])} groups of byte-identical files')


if __name__ == '__main__':
    main()
