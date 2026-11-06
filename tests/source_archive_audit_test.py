"""Negative payload tests for the release-specific source audit (no game files)."""
import importlib.util
import io
from pathlib import Path
import random
import tarfile
import unittest

REPO = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    'source_audit', REPO / 'tools/release/audit_macos2_source.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class SourceArchiveAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.expected, cls.bad_ext, cls.bad_name = audit.reference(REPO)
        cls.canonical = audit.git(REPO, 'archive', '--format=tar',
                                  '--prefix=' + audit.PREFIX + '/', audit.RELEASE)

    def altered(self, edits=None, extra=None, omit=None, special=None):
        output = io.BytesIO()
        with tarfile.open(fileobj=io.BytesIO(self.canonical)) as src, \
                tarfile.open(fileobj=output, mode='w') as dst:
            for member in src:
                relative = member.name.removeprefix(audit.PREFIX + '/').rstrip('/')
                if relative == omit:
                    continue
                content = src.extractfile(member).read() if member.isfile() else None
                if edits and relative in edits:
                    content = edits[relative]
                    member.size = len(content)
                dst.addfile(member, io.BytesIO(content) if content is not None else None)
            if extra:
                path, content = extra
                member = tarfile.TarInfo(audit.PREFIX + '/' + path)
                member.size = len(content)
                member.mode = 0o644
                dst.addfile(member, io.BytesIO(content))
            if special:
                dst.addfile(special)
        return output.getvalue()

    def issues(self, data):
        return audit.audit_contents(data, self.expected, self.bad_ext, self.bad_name)

    def test_exact_source_passes(self):
        self.assertEqual(self.issues(self.canonical), [])

    def test_added_generated_shader_fails_even_with_public_template_bytes(self):
        shader = self.expected[0]['gtav/src/native/shaders/native_memory.hlsl'][0]
        problems = self.issues(self.altered(extra=('vs_0123456789ABCDEF.hlsl', shader)))
        self.assertTrue(any('prohibited generated/game filename' in p for p in problems))
        self.assertTrue(any('prohibited extension' in p for p in problems))

    def test_game_archive_fails(self):
        problems = self.issues(self.altered(extra=('data/update.rpf', b'SYNTHETIC GAME PAYLOAD')))
        self.assertTrue(any('prohibited' in p for p in problems))

    def test_translated_spirv_fails(self):
        problems = self.issues(self.altered(extra=('shader_cache/renamed.spv', b'\x03\x02\x23\x07test')))
        self.assertTrue(any('prohibited extension' in p for p in problems))

    def test_renamed_game_data_cannot_hide_as_cpp(self):
        problems = self.issues(self.altered(extra=('gtav/src/innocent.cpp', b'SYNTHETIC GAME PAYLOAD')))
        self.assertTrue(any('unexpected file' in p for p in problems))

    def test_game_data_in_existing_text_file_fails(self):
        problems = self.issues(self.altered(edits={'README.md': b'SYNTHETIC GAME PAYLOAD'}))
        self.assertTrue(any('content/mode differs' in p for p in problems))

    def test_allowed_hlsl_path_does_not_allow_changed_content(self):
        path = 'gtav/src/native/shaders/native_memory.hlsl'
        content = self.expected[0][path][0] + b'\n// SYNTHETIC GENERATED SHADER INJECTION\n'
        problems = self.issues(self.altered(edits={path: content}))
        self.assertTrue(any('unreviewed HLSL' in p for p in problems))

    def test_altered_template_span_fails(self):
        path, offset, _, _ = audit.SPANS[1]
        content = bytearray(self.expected[0][path][0]); content[offset] ^= 1
        problems = self.issues(self.altered(edits={path: bytes(content)}))
        self.assertTrue(any('template span changed' in p for p in problems))

    def test_missing_file_fails(self):
        self.assertTrue(any('missing file' in p for p in self.issues(self.altered(omit='README.md'))))

    def test_duplicate_member_fails(self):
        self.assertTrue(any('duplicate' in p for p in self.issues(
            self.altered(extra=('README.md', b'conflicting duplicate')))))

    def test_path_traversal_fails(self):
        self.assertTrue(any('unsafe' in p for p in self.issues(
            self.altered(extra=('../escape.cpp', b'injection')))))

    def test_symlink_fails(self):
        member = tarfile.TarInfo(audit.PREFIX + '/symlink')
        member.type = tarfile.SYMTYPE; member.linkname = '/private/secret'
        self.assertTrue(any('link or special' in p for p in self.issues(self.altered(special=member))))

    def test_endian_sequence_matches_independent_byte_permutations(self):
        # The shared Swap/vfetchSwap operation is generic GPU byte order handling.
        permutations = ((0, 1, 2, 3), (1, 0, 3, 2), (3, 2, 1, 0), (2, 3, 0, 1))
        rng = random.Random(20261105)
        for original in [0, 0xffffffff, 0x12345678] + [rng.getrandbits(32) for _ in range(1000)]:
            for endian, permutation in enumerate(permutations):
                value = original
                if endian in (1, 2):
                    value = (((value & 0x00ff00ff) << 8) | ((value >> 8) & 0x00ff00ff)) & 0xffffffff
                if endian in (2, 3):
                    value = ((value << 16) | (value >> 16)) & 0xffffffff
                raw = original.to_bytes(4, 'big')
                expected = int.from_bytes(bytes(raw[i] for i in permutation), 'big')
                self.assertEqual(value, expected)


if __name__ == '__main__':
    unittest.main()
