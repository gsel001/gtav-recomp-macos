"""Fail-closed provenance audit for the EXACT macos.2 source archive.

This is not a general content scanner and does not replace taint_scan.py's
independent binary gate. New source, paths, hashes or findings require review.
No game-derived file is read, copied into a control set, or executed here.
"""
import argparse
import ast
import hashlib
import io
from pathlib import Path, PurePosixPath
import re
import subprocess
import tarfile

RELEASE = 'bbdf1da008a98db4390e5b50a08ff4f7bbde278d'
UPSTREAM = '2a76184f563e4b604a88bea8eeed1334e77f3220'
PREFIX = 'gtav-recomp-macos-v0.3.1-macos.2'
ARCHIVE_SHA256 = '543c374dfeaa9737dd5ba5c08a09beecc1f45f7cf5bdff482e477090db3fbcd1'
SCANNER_SHA256 = '23ef2f59dec19c0696b7c31eca4ba7372e988157b976e231d75ca7c0e71ede51'
ORIGINAL_LOG_SHA256 = 'ff9345c9c26a90f823889c3dba1606fa813a1e3f71bbabafb5bde0e2818d38c6'
PUBLIC_BLOBS = {
    'gtav/src/native/shaders/native_blit.hlsl': 'eaaa9f3ce52efbbe147026d4f050bce863a5b204',
    'gtav/src/native/shaders/native_memory.hlsl': 'a87c39d1f1ca3f6b0897d3b6b57cdec161fa5812',
    'gtav/src/native/shaders/native_menu.hlsl': 'aed13fd4340984acae98922858a72f86443d6740',
    'gtav/src/native/shaders/native_rectlist.hlsl': 'b6311838b174db82f9726dd52524b4081eab6099',
    'xenosrecomp-gtav.patch': 'aae7517cd37bdbc0f5bc26e56c11c11f40e63819',
    'xenosrecomp-win-before-linux-1029.patch': 'ed0fe0d470dc1c979a7d380d4644b12f4d13a43f',
}
# Only these four full paths AND exact reviewed blobs may have a .hlsl suffix.
AUTHORED_HLSL = {p for p in PUBLIC_BLOBS if p.endswith('.hlsl')}
# Reviewed public-template spans. Length is raw contiguous bytes, not the
# original scanner's content_length score (respectively 176, 136 and 144).
SPANS = (
    ('gtav/src/native/shaders/native_memory.hlsl', 0x5e5, 229,
     '187ab143febef0fb93b01aa652b27688942465c19c383d42c49c5c7b86620bc1'),
    ('xenosrecomp-gtav.patch', 0x1612, 161,
     'f5a011daa277d2467b72d47e2eaa7ac5c6fa7f961b101b4e8d05bf8c6c4c5b4f'),
    ('xenosrecomp-win-before-linux-1029.patch', 0x1a88, 171,
     'dd7757160c6187077b2251b7169a9a1ed53bad877b9c3098db10eaf80a097d4e'),
)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def blob_id(data):
    return hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()

def git(repo, *args):
    return subprocess.check_output(['git', '--no-replace-objects', '-C', str(repo), *args])

def read_members(data):
    """Inspect in memory; never extract or follow archive links."""
    files, dirs, seen = {}, set(), set()
    total = 0
    with tarfile.open(fileobj=io.BytesIO(data), mode='r|*') as archive:
        for member in archive:
            path = PurePosixPath(member.name)
            parts = member.name.rstrip('/').split('/')
            if (path.is_absolute() or any(p in ('', '.', '..') for p in parts)
                    or parts[0] != PREFIX or member.name in seen):
                raise ValueError('unsafe or duplicate archive path: ' + member.name)
            seen.add(member.name)
            relative = '/'.join(parts[1:])
            if member.isdir():
                dirs.add(relative)
            elif member.isfile() and relative:
                if relative in files or member.size > 16 * 1024 * 1024:
                    raise ValueError('duplicate or oversized file: ' + relative)
                total += member.size
                if total > 64 * 1024 * 1024:
                    raise ValueError('source archive too large')
                files[relative] = (archive.extractfile(member).read(), member.mode)
            else:
                raise ValueError('link or special archive member: ' + member.name)
    return files, dirs

def reference(repo):
    """Trust pinned Git objects, not the contents or HEAD of a working tree."""
    for path, oid in PUBLIC_BLOBS.items():
        for revision in (RELEASE, UPSTREAM):
            if blob_id(git(repo, 'show', revision + ':' + path)) != oid:
                raise ValueError('public provenance mismatch: ' + path)
    scanner = git(repo, 'show', RELEASE + ':tools/release/taint_scan.py')
    if sha(scanner) != SCANNER_SHA256:
        raise ValueError('pinned scanner object changed')
    local = Path(repo) / 'tools/release/taint_scan.py'
    if sha(local.read_bytes()) != SCANNER_SHA256:
        raise ValueError('working scanner changed')
    # Read the ORIGINAL policy constants without importing numpy or executing it.
    nodes = {n.targets[0].id: n.value for n in ast.parse(scanner).body
             if isinstance(n, ast.Assign) and isinstance(n.targets[0], ast.Name)}
    bad_ext = ast.literal_eval(nodes['BAD_EXT'])
    bad_name = re.compile(ast.literal_eval(nodes['BAD_NAME'].args[0]), re.I)
    canonical = git(repo, 'archive', '--format=tar', '--prefix=' + PREFIX + '/', RELEASE)
    return read_members(canonical), bad_ext, bad_name

def audit_contents(data, expected, bad_ext, bad_name):
    """Every unexpected file/change fails, including a renamed game payload."""
    issues = []
    try:
        actual, dirs = read_members(data)
    except (ValueError, tarfile.TarError, OSError) as error:
        return [str(error)]
    wanted, wanted_dirs = expected
    for path in sorted(actual):
        content, mode = actual[path]
        if bad_name.match(PurePosixPath(path).name):
            issues.append('prohibited generated/game filename: ' + path)
        if PurePosixPath(path).suffix.lower() in bad_ext:
            if path not in AUTHORED_HLSL or blob_id(content) != PUBLIC_BLOBS[path]:
                issues.append('prohibited extension or unreviewed HLSL: ' + path)
        if path not in wanted:
            issues.append('unexpected file: ' + path)
        elif (content, mode) != wanted[path]:
            issues.append('content/mode differs from pinned release: ' + path)
    issues += ['missing file: ' + path for path in sorted(wanted.keys() - actual.keys())]
    if dirs != wanted_dirs:
        issues.append('directory inventory differs from pinned release')
    for path, offset, size, digest in SPANS:
        if path not in actual or sha(actual[path][0][offset:offset + size]) != digest:
            issues.append('reviewed public-template span changed: ' + path)
    return issues

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--repository', type=Path, required=True)
    parser.add_argument('--original-scan', type=Path, required=True)
    args = parser.parse_args()
    try:
        if sha(args.original_scan.read_bytes()) != ORIGINAL_LOG_SHA256:
            raise ValueError('original failed scan was changed or replaced')
        if args.archive.stat().st_size > 32 * 1024 * 1024:
            raise ValueError('archive too large')
        data = args.archive.read_bytes()
        expected, bad_ext, bad_name = reference(args.repository)
        issues = audit_contents(data, expected, bad_ext, bad_name)
        if sha(data) != ARCHIVE_SHA256:
            issues.append('recorded release archive SHA-256 mismatch')
        if issues:
            for issue in issues:
                print('FAIL:', issue)
            return 1
        print('PASS: exact macos.2 source provenance/inventory; 261 files; seven findings resolved')
        print('Original combined scan remains FAIL: 7; independent binary scan is a separate gate.')
        print('Publication still requires human review. This tool never publishes.')
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print('FAIL:', error)
        return 1

if __name__ == '__main__':
    raise SystemExit(main())
