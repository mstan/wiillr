# SPDX-License-Identifier: MIT
"""Copy supplied NAND inputs into an explicitly owned writable runtime profile."""
from pathlib import Path
import hashlib
import json
import shutil
import stat
import struct

MAGIC = 'wiillr-copied-nand-v1'


def check_chain(root):
    for parent in (root, *root.parents):
        if not parent.exists() and not parent.is_symlink():
            continue
        info = parent.lstat()
        if parent.is_symlink() or getattr(info, 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise ValueError('NAND root cannot traverse links or reparse points')


def snapshot(root):
    root = root.absolute()
    entries = {}
    def visit(directory):
        for path in sorted(directory.iterdir()):
            info = path.lstat()
            if path.is_symlink() or getattr(info, 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
                raise ValueError('NAND inputs/profile cannot contain links or reparse points')
            if path.is_dir():
                visit(path)
            elif path.is_file():
                with path.open('rb') as stream:
                    digest = hashlib.sha256()
                    for chunk in iter(lambda: stream.read(1024 * 1024), b''):
                        digest.update(chunk)
                    entries[path.relative_to(root).as_posix()] = digest.hexdigest()
            else:
                raise ValueError('NAND contains an unsupported host entry')
    check_chain(root)
    visit(root)
    return entries


def prepare(source, destination, title):
    source, destination = source.absolute(), destination.absolute()
    check_chain(source)
    check_chain(destination)
    source, destination = source.resolve(strict=True), destination.resolve()
    if source == destination or source in destination.parents or destination in source.parents:
        raise ValueError('Writable profile and source NAND must have separate roots')
    original = snapshot(source)
    marker = destination / '.wiillr-nand-profile'
    manifest = destination / '.wiillr-nand-source.json'
    if destination.exists():
        if marker.read_text() != MAGIC + '\n':
            raise ValueError('Existing NAND profile lacks the ownership marker')
        if json.loads(manifest.read_text()) != dict(source=str(source), files=original):
            raise ValueError('Existing NAND profile belongs to different original inputs')
        snapshot(destination)  # Reject reparse points before the runtime can write.
    else:
        destination.mkdir(parents=True, exist_ok=False)
        for relative in original:
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / relative, target)
        if snapshot(destination) != original:
            raise ValueError('NAND profile copy verification failed')
        manifest.write_text(json.dumps(dict(source=str(source), files=original), indent=2) + '\n')
        marker.write_text(MAGIC + '\n')
    raw = (destination / 'sys/uid.sys').read_bytes()
    if len(raw) % 12:
        raise ValueError('Truncated NAND UID table')
    users = {}
    for at in range(0, len(raw), 12):
        tid, uid = struct.unpack_from('>QI', raw, at)
        if tid in users or uid in users.values():
            raise ValueError('Duplicate NAND UID table identity')
        users[tid] = uid
    if int(title, 16) not in users:
        raise ValueError('Boot title is absent from the supplied NAND UID table')
    return original, users[int(title, 16)]
