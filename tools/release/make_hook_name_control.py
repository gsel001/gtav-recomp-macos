"""Build a taint-scan control containing only public hook/import name metadata.

The compiled game library and frontend share linker symbol-name strings. Verify
those strings against our own generated hook declarations and authored REX_EXTERN
references, and only accept blocks inside Mach-O's __LINKEDIT segment. No machine
instructions, game shader bytes or arbitrary binary ranges are accepted.
"""
import argparse
from pathlib import Path
import re
import struct

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('executable', type=Path)
p.add_argument('--hooks', type=Path, required=True)
p.add_argument('--source', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
data = a.executable.read_bytes()
assert data[:4] == b'\xcf\xfa\xed\xfe', 'Expected little-endian 64-bit Mach-O'
linkedit = None
at = 32
for _ in range(struct.unpack_from('<I', data, 16)[0]):
    command, size = struct.unpack_from('<II', data, at)
    assert size >= 8
    if command == 0x19 and data[at + 8:at + 24].split(b'\0')[0] == b'__LINKEDIT':
        off, length = struct.unpack_from('<QQ', data, at + 40)
        linkedit = (off, off + length)
    at += size
assert linkedit, 'Missing __LINKEDIT'
allowed = set(re.findall(rb'GTAVFS_HOOKED_(sub_[0-9A-F]{8})', a.hooks.read_bytes()))
for path in a.source.rglob('*'):
    if path.suffix in {'.cpp', '.h'}:
        allowed.update(re.findall(rb'REX_EXTERN\(__imp__(sub_[0-9A-F]{8})\)', path.read_bytes()))
blocks = []
for match in re.finditer(rb'\x00(?:(?:___imp__|_)sub_[0-9A-F]{8}\x00)+', data):
    if not (linkedit[0] <= match.start() and match.end() <= linkedit[1]):
        continue
    names = re.findall(rb'(?:___imp__|_)(sub_[0-9A-F]{8})', match[0])
    assert all(name in allowed for name in names), 'Name not declared by this project'
    blocks.append(match[0])
assert blocks, 'No verified name blocks found'
a.out.write_bytes(b'\n'.join(blocks))
print('Verified public linker-name metadata:', len(blocks), 'blocks,', sum(map(len, blocks)), 'bytes')
