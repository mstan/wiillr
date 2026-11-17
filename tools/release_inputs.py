#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Extract a checksum-pinned private build-input archive without path escapes."""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path, PurePosixPath
import shutil
import stat
import zipfile


def unpack(archive: Path, output: Path, expected: str) -> None:
    digest = hashlib.sha256()
    with archive.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    if digest.hexdigest() != expected.lower():
        raise ValueError('Build-input archive checksum differs')
    if output.exists() and any(output.iterdir()):
        raise ValueError('Build-input destination must be empty')
    output.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as source:
        entries = source.infolist()
        names: set[str] = set()
        for entry in entries:
            path = PurePosixPath(entry.filename)
            if (not path.parts or path.is_absolute() or '..' in path.parts or
                    any(part.lower() == '.git' for part in path.parts) or
                    '\\' in entry.filename or ':' in entry.filename or
                    stat.S_ISLNK(entry.external_attr >> 16) or entry.filename in names):
                raise ValueError('Invalid build-input member: ' + entry.filename)
            names.add(entry.filename)
        if 'inputs.json' not in names:
            raise ValueError('Build-input archive lacks inputs.json')
        for entry in entries:
            target = output.joinpath(*PurePosixPath(entry.filename).parts)
            if entry.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with source.open(entry) as src, target.open('xb') as dst:
                shutil.copyfileobj(src, dst)
            mode = (entry.external_attr >> 16) & 0o777
            if mode:
                target.chmod(mode)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--sha256', required=True)
    options = parser.parse_args()
    unpack(options.archive, options.output, options.sha256)
