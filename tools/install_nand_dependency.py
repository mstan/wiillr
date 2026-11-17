# SPDX-License-Identifier: GPL-3.0-or-later
"""Merge one verified original IOS title into a stopped, owned NAND copy."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct

import nand_profile
from wii_inputs import title_metadata


def _read(root, relative, optional=False):
    path = root / relative
    nand_profile.check_chain(path)
    if optional and not path.exists():
        return b''
    return path.read_bytes()


def _users(raw):
    if len(raw) % 12:
        raise ValueError('Truncated UID table')
    rows = list(struct.iter_unpack('>QI', raw))
    if len({t for t, u in rows}) != len(rows) or len({u for t, u in rows}) != len(rows):
        raise ValueError('Duplicate UID identity')
    return dict(rows)


def _shared(raw):
    if len(raw) % 28:
        raise ValueError('Truncated shared content map')
    names, hashes = set(), {}
    for at in range(0, len(raw), 28):
        name, digest = raw[at:at + 8].decode('ascii'), raw[at + 8:at + 28].hex()
        if len(name) != 8 or any(c not in '0123456789abcdefABCDEF' for c in name):
            raise ValueError('Invalid shared content filename')
        if name.lower() in names or digest in hashes:
            raise ValueError('Duplicate shared content identity')
        names.add(name.lower())
        hashes[digest] = name
    return hashes


def _ticket(raw, title):
    at = 0
    while at < len(raw):
        if len(raw) - at < 4:
            raise ValueError('Truncated ticket')
        base = {0x10000: 0x240, 0x10001: 0x140, 0x10002: 0x80}.get(int.from_bytes(raw[at:at + 4], 'big'))
        if not base or len(raw) - at < base + 0x164:
            raise ValueError('Unsupported or truncated ticket')
        body = raw[at + base:at + base + 0x164]
        if body[0x7c] or int.from_bytes(body[0x9c:0xa4], 'big') != title:
            raise ValueError('Ticket and title identities differ')
        at += base + 0x164
    if not at:
        raise ValueError('Missing original ticket')


def install(source, profile, title):
    source, profile = Path(source).absolute(), Path(profile).absolute()
    nand_profile.check_chain(source)
    nand_profile.check_chain(profile)
    source, profile = source.resolve(strict=True), profile.resolve(strict=True)
    if source == profile or source in profile.parents or profile in source.parents:
        raise ValueError('Source NAND and writable profile need separate roots')
    if _read(profile, '.wiillr-nand-profile').decode('utf-8').replace('\r\n', '\n') != nand_profile.MAGIC + '\n':
        raise ValueError('Install only into a marked copied NAND profile')
    if len(title) != 16 or any(c not in '0123456789abcdef' for c in title):
        raise ValueError('Use a lowercase 16-digit IOS title ID')
    tid = int(title, 16)
    if tid >> 32 != 1 or tid & 0xffffffff < 3:
        raise ValueError('Dependency must be an IOS system title')
    prefix = f'title/{title[:8]}/{title[8:]}/content'
    tmd_path, ticket_path = f'{prefix}/title.tmd', f'ticket/{title[:8]}/{title[8:]}.tik'
    tmd, ticket = _read(source, tmd_path), _read(source, ticket_path)
    info = title_metadata(tmd)
    if info['title_id'] != title or not info['contents']:
        raise ValueError('TMD directory/title identity mismatch')
    _ticket(ticket, tid)
    if tid not in _users(_read(source, 'sys/uid.sys')):
        raise ValueError('Source IOS is absent from its UID table')
    source_shared = _shared(_read(source, 'shared1/content.map', True))
    mapping = _read(profile, 'shared1/content.map', True)
    shared = _shared(mapping)
    users_raw = _read(profile, 'sys/uid.sys')
    users = _users(users_raw)
    outputs, reused = {}, 0
    occupied = {n.lower() for n in shared.values()}
    directory = profile / 'shared1'
    nand_profile.check_chain(directory)
    if directory.exists():
        occupied.update(p.stem.lower() for p in directory.glob('*.app'))
    for content in info['contents']:
        if content['flags'] not in (1, 0x8001):
            raise ValueError('Unsupported IOS content flags')
        if content['flags'] & 0x8000:
            name = source_shared.get(content['sha1'])
            if name is None:
                raise ValueError('Missing original shared content identity')
            relative = f'shared1/{name}.app'
        else:
            relative = f"{prefix}/{content['id']}.app"
        data = _read(source, relative)
        if len(data) != content['size'] or hashlib.sha1(data).hexdigest() != content['sha1']:
            raise ValueError('Original content size/hash verification failed')
        if content['flags'] & 0x8000:
            name = shared.get(content['sha1'])
            if name is None:
                number = 0
                while f'{number:08x}' in occupied:
                    number += 1
                if number > 0xffffffff:
                    raise ValueError('Shared content identity space exhausted')
                name = f'{number:08x}'
                occupied.add(name)
                shared[content['sha1']] = name
                mapping += name.encode() + bytes.fromhex(content['sha1'])
            else:
                reused += 1
            relative = f'shared1/{name}.app'
        outputs[relative] = data
    uid = users.get(tid)
    if uid is None:
        uid = max([0xfff, *users.values()]) + 1
        if uid > 0xffffffff:
            raise ValueError('UID identity space exhausted')
        outputs['sys/uid.sys'] = users_raw + struct.pack('>QI', tid, uid)
    if mapping != _read(profile, 'shared1/content.map', True):
        outputs['shared1/content.map'] = mapping
    outputs[tmd_path] = tmd
    existing_ticket = _read(profile, ticket_path, True)
    if existing_ticket:
        _ticket(existing_ticket, tid)  # Preserve an already imported license.
    else:
        outputs[ticket_path] = ticket
    mutable = {'sys/uid.sys', 'shared1/content.map'}
    # Validate every original payload and destination conflict before writing.
    for relative, data in outputs.items():
        target = profile / relative
        nand_profile.check_chain(target)
        if target.exists() and relative not in mutable and target.read_bytes() != data:
            raise ValueError('Conflicting installed content: ' + relative)
    changed = {}
    # Each file is atomic, as in the existing offline WAD installer. An I/O
    # failure leaves a resumable owned copy; callers launch only after success.
    for relative, data in outputs.items():
        target = profile / relative
        if target.exists() and target.read_bytes() == data:
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        temporary = target.with_name(target.name + '.wiillr-dependency-tmp')
        nand_profile.check_chain(temporary)
        with temporary.open('xb') as stream:
            stream.write(data)
        os.replace(temporary, target)
        if target.read_bytes() != data:
            raise OSError('Installed dependency verification failed')
        changed[relative] = hashlib.sha256(data).hexdigest()
    return dict(title=title, version=info['version'], uid=uid, contents=len(info['contents']),
                shared_reused=reused, content_hashes_verified=True, signatures_verified=False,
                existing_license_preserved=bool(existing_ticket), profile=str(profile), changed=changed,
                source_tmd_sha256=hashlib.sha256(tmd).hexdigest(),
                source_ticket_sha256=hashlib.sha256(ticket).hexdigest(), booted=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--profile', type=Path, required=True)
    parser.add_argument('--title', required=True)
    parser.add_argument('--receipt', type=Path, required=True)
    args = parser.parse_args()
    if args.receipt.exists():
        raise FileExistsError('Use a fresh installation receipt')
    result = install(args.source, args.profile, args.title)
    args.receipt.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: result[k] for k in ('title', 'contents', 'content_hashes_verified', 'booted')}))


if __name__ == '__main__':
    main()
