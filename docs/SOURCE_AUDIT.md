# macos.2 source-archive audit

Audit date: November 5, 2026. Release source commit: `bbdf1da008a98db4390e5b50a08ff4f7bbde278d`.

**Technical verdict: all seven original findings are explained false positives for this exact source archive. No unexplained positive remains. Ready for final human review; not authorization to publish.** The GitHub prerelease remains a draft. Neither binaries nor source archives were replaced. The original combined scan still records `FAIL: 7 problem(s)`; it has not been edited or relabeled as a passing scan.

The original scanner is designed to reject shader files in a binary package. The four filename findings concern project-authored renderer utility source files. The three byte findings are shared public compiler-support routines, not shader-specific instructions recovered from GTA V. The evidence below establishes the origin of the actual matching ranges, rather than assuming that public availability alone proves provenance.

This PR contains the review supplement and audit code only. The original failed scanner output remains private local evidence identified by its digest; it is not added to Git or release assets. Personal runtime paths below are redacted.

## Material examined and preservation

- Read the complete original `VALIDATION.txt`, original combined-scan output, unmodified `taint_scan.py`, the four utility shaders, and the implicated patch hunks and compiler/SDK sources.
- Original combined output: `/private/tmp/gtav-macos-2-scan.log`; preserved separately in the project workspace at `work/gtav-source-audit/evidence/original-combined-scan.txt`. SHA-256: `ff9345c9c26a90f823889c3dba1606fa813a1e3f71bbabafb5bde0e2818d38c6`.
- Original binary output: `/private/tmp/gtav-macos-2-binary-scan.log`; preserved at `work/gtav-source-audit/evidence/original-binary-scan.txt`. SHA-256: `d6f7b7805be2395f959774aaf6f4f3fe659200657d8bf324b756345fc0dc6971`.
- Original scanner SHA-256: `23ef2f59dec19c0696b7c31eca4ba7372e988157b976e231d75ca7c0e71ede51`. It is unchanged from the pinned release Git object. No threshold, HLSL rule, match routine or control set was changed.
- Independently fetched upstream and downstream shader-directory metadata through GitHub at the exact commits, then verified local Git objects and actual archived bytes. Also independently fetched the public compiler/SDK files used below and compared them byte for byte with the dependency-source archive.
- Local comparison shader was read only to reproduce the three ranges. No installed app, runtime, shader cache, setting or save was modified. No game-derived comparison file or shader body was copied into a control set, regression fixture or deliverable.
- Private original logs remain local evidence; they are not added to the release assets. `VALIDATION.txt` and the original `SHA256SUMS` remain unchanged.

## Seven-finding evidence table

Paths below are relative to source-archive root `gtav-recomp-macos-v0.3.1-macos.2/`. Original byte findings used that root inside the combined scan directory. All offsets are zero-based file-byte offsets; ranges are half-open. The comparison input for B1–B3 is the same file, identified in the next section.

| ID | Original finding / archive path | Exact evidence | Classification |
|---|---|---|---|
| N1 | `name: gtav/src/native/shaders/native_memory.hlsl` | Upstream and downstream Git blob `a87c39d1f1ca3f6b0897d3b6b57cdec161fa5812`; archived bytes match. [Pinned source](https://github.com/andrewnakas/gtav-recomp/blob/2a76184f563e4b604a88bea8eeed1334e77f3220/gtav/src/native/shaders/native_memory.hlsl) defines an explicit host resolve-to-memory utility with push constants and a compute entry point. | Filename-rule false positive for authored source. Only this exact path and blob are admitted by the separate audit. |
| N2 | `name: gtav/src/native/shaders/native_blit.hlsl` | Both trees: `eaaa9f3ce52efbbe147026d4f050bce863a5b204`; archive matches. [Pinned source](https://github.com/andrewnakas/gtav-recomp/blob/2a76184f563e4b604a88bea8eeed1334e77f3220/gtav/src/native/shaders/native_blit.hlsl) contains presentation, resolve and diagnostic utility entry points. | Filename-rule false positive; not a translated `vs_<hash>`/`ps_<hash>` game shader. |
| N3 | `name: gtav/src/native/shaders/native_rectlist.hlsl` | Both trees: `b6311838b174db82f9726dd52524b4081eab6099`; archive matches. [Pinned source](https://github.com/andrewnakas/gtav-recomp/blob/2a76184f563e4b604a88bea8eeed1334e77f3220/gtav/src/native/shaders/native_rectlist.hlsl) expands a rectangle's fourth vertex geometrically. | Filename-rule false positive; authored geometry utility. |
| N4 | `name: gtav/src/native/shaders/native_menu.hlsl` | Both trees: `aed13fd4340984acae98922858a72f86443d6740`; archive matches. [Pinned source](https://github.com/andrewnakas/gtav-recomp/blob/2a76184f563e4b604a88bea8eeed1334e77f3220/gtav/src/native/shaders/native_menu.hlsl) implements the host ImGui menu's vertex/color/texture path. | Filename-rule false positive; authored host UI shader. |
| B1 | `bytes: gtav/src/native/shaders/native_memory.hlsl` | Archive `[0x5e5,0x6ca)` (line 30 onward), comparison `[0x2fc1,0x30a6)` (line 305 onward). **229 contiguous bytes; scanner score 176**. Complete body of `Swap`, identical to public common-header `vfetchSwap` body. | Shared public endian-conversion helper; no game-specific shader instructions in this span. |
| B2 | `bytes: xenosrecomp-gtav.patch` | Archive `[0x1612,0x16b3)` (line 119), comparison `[0x1471,0x1512)` (line 125). **161 contiguous bytes; score 136**. Added `tfetch2D` return expression, excluding the patch's leading `+`. | Public compiler common-header texture-sampling expression; same code inserted into every translated shader. |
| B3 | `bytes: xenosrecomp-win-before-linux-1029.patch` | Archive `[0x1a88,0x1b33)` (line 137), comparison `[0x1ff6,0x20a1)` (line 226). **171 contiguous bytes; score 144**. Added `tfetchCube` return expression, excluding the patch's leading `+`. | Public common-header cubemap-sampling expression, also present in the active public patch. |

The two patch blobs, independently checked against public upstream `2a76184f563e4b604a88bea8eeed1334e77f3220`, are respectively `aae7517cd37bdbc0f5bc26e56c11c11f40e63819` and `ed0fe0d470dc1c979a7d380d4644b12f4d13a43f`. Their full files, not just these snippets, match downstream and the archive.

### Exact comparison source and match identity

All three original byte findings refer to:

`<private-runtime>/data/shader_cache/vs_188AF0A17175FE2A.hlsl`

Only that one matching basename exists in the original scanner's shader comparison directories. Its read-only SHA-256 is `563c236b11c304f7b9cf5a5c43bcc82bcc072924353549c1f35ac00eba00bcfd`.

| Finding | SHA-256 of the complete contiguous common sequence |
|---|---|
| B1, 229 bytes | `187ab143febef0fb93b01aa652b27688942465c19c383d42c49c5c7b86620bc1` |
| B2, 161 bytes | `f5a011daa277d2467b72d47e2eaa7ac5c6fa7f961b101b4e8d05bf8c6c4c5b4f` |
| B3, 171 bytes | `dd7757160c6187077b2251b7169a9a1ed53bad877b9c3098db10eaf80a097d4e` |

The original scanner labels its `content_length` result as a run length. That function scores selected nontrivial windows in increments of eight bytes; it does **not** return the full contiguous length. We grew each match at its original offsets, verified complete byte equality, reproduced scores 176/136/144 with the unchanged function, and recovered raw lengths 229/161/171. The earlier validation report's “runs (176/136/144 bytes)” wording is therefore clarified here without rewriting the historical asset.

## Independent compiler provenance

### Public input, deterministic inclusion, exact output prefix

The dependency archive documents XenosRecomp commit `990d03b28a27b50277ee5d8d942e1c5f873869d1`. Independently fetched files at that commit match the archive bytes:

| Public file | SHA-256 |
|---|---|
| [XenosRecomp/shader_common.h](https://github.com/hedge-dev/XenosRecomp/blob/990d03b28a27b50277ee5d8d942e1c5f873869d1/XenosRecomp/shader_common.h) | `8130e8914491879a3905512c941f5a603b789d36edd97913879eeca691f32a43` |
| [XenosRecomp/shader_recompiler.cpp](https://github.com/hedge-dev/XenosRecomp/blob/990d03b28a27b50277ee5d8d942e1c5f873869d1/XenosRecomp/shader_recompiler.cpp) | `025afceba969e505f10af1220cb4dca603966e71464bff3fc24419f7b2224b70` |
| [XenosRecomp/main.cpp](https://github.com/hedge-dev/XenosRecomp/blob/990d03b28a27b50277ee5d8d942e1c5f873869d1/XenosRecomp/main.cpp) | `d9baecb242c961f81744f9c15f6a0c4ff47db46c0cd1e36c2a4b28d6033436ed` |

The public command-line driver reads a separate common-header argument at lines 58–68 and passes it into the recompiler at lines 124/214. The recompiler appends that supplied text to its output at lines 1101–1109, before shader-specific constant/instruction generation. This is an explicit author-controlled support header, rather than a header reconstructed from shader instructions.

Applied the pinned [public project patch](https://github.com/andrewnakas/gtav-recomp/blob/2a76184f563e4b604a88bea8eeed1334e77f3220/xenosrecomp-gtav.patch) to a separate offline copy of the independently verified compiler sources. No game input was needed. The resulting `shader_common.h` has **17,591 bytes**, SHA-256 `7fb61cb612b403e7ee27bbb2d572096449c1c50573389f1a7e7475fb50384faf`.

That entire public header occurs **verbatim at offset zero** in the comparison shader. Every B1–B3 comparison range lies within this prefix. Therefore the shared bytes have a demonstrated public-template origin; finding them in a game-derived output does not make the underlying author-supplied helper game-derived. No comparison-input bytes were used to manufacture the header or an exemption control.

### B1: generic byte order

`native_memory.hlsl`'s `Swap` body performs adjacent-byte swaps for modes 1/2, then exchanges halfwords for modes 2/3, returning the word. Its second operation is `w = (w << 16) | (w >> 16)`. The exact same function body is authored in the public compiler patch's `vfetchSwap` helper.

Independent corroboration is available in [ReXGlue SDK xenos.h at f5337cdc947ff6d4c4196737e2c807a48f2a1fc2](https://github.com/rexglue/rexglue-sdk/blob/f5337cdc947ff6d4c4196737e2c807a48f2a1fc2/include/rex/graphics/xenos.h): lines 192–197 specify the four endian modes; lines 1039–1056 implement their generic word conversions. The independently fetched file matches the dependency source, SHA-256 `f7dcbf856feaeb0350c9c9aaf5c16a768b4147fc487819cd513e0cc68fc16fd5`.

The regression suite cross-checks all modes against independent byte permutations for fixed cases and 1,000 deterministic random words. The common sequence contains no guest program addresses, shader instruction stream, material constants, texture contents or game-specific control flow. The surrounding authored utility may implement behavior needed by GTA V; that does not change the generic provenance of this specific matched span.

### B2: public two-dimensional texture fetch

The sequence is the complete return expression inside patched `tfetch2D`. It selects a sampler using a function parameter, adjusts normalized texture coordinates by the supplied offset plus `XENOS_FETCH_ROUNDING`, divides by runtime texture dimensions and invokes the public stage-dependent sampling macro. It contains parameter names, not a fixed game's resource bindings or instruction sequence.

The original public XenosRecomp header already defines this parameterized helper at lines 62–66. The public project patch changes the sampling macro and rounding expression. The rounding value is independently documented in [ReXGlue SDK spirv_translator_fetch.cpp at f5337cdc947ff6d4c4196737e2c807a48f2a1fc2](https://github.com/rexglue/rexglue-sdk/blob/f5337cdc947ff6d4c4196737e2c807a48f2a1fc2/src/graphics/pipeline/shader/spirv_translator_fetch.cpp), lines 679–716, including the same 1.5/1024 texel adjustment and explanations involving other games. Independently fetched SDK file SHA-256: `b4cba3533cb7f5bdf92206493b49ded9e4e781194a23d363ffabd06ebda77e39`.

This is a public renderer/compiler implementation adjustment motivated by compatibility, not copied GTA V shader instructions. Reapplying the patch reproduces the exact return line without reading any game.

### B3: public cube texture fetch

The sequence is the complete return expression inside `tfetchCube`: a texture descriptor, sampler and direction are selected from function parameters and `cubeMapData.cubeMapDirections[texCoord.z]`. The public unpatched XenosRecomp header defines the helper at lines 157–159. The older public patch wraps its existing sample call in the stage-dependent sampling macro; the active public patch contains the same line at line 165.

The older patch is [public at the pinned upstream commit](https://github.com/andrewnakas/gtav-recomp/blob/2a76184f563e4b604a88bea8eeed1334e77f3220/xenosrecomp-win-before-linux-1029.patch). The actual comparison match is reproduced by the active patched common header, so there is no need to assume which historical compiler binary produced the local cache. The exact common text has no shader-specific literals or instructions.

## Separate narrow source audit and regression protection

Added `tools/release/audit_macos2_source.py` and `tests/source_archive_audit_test.py`. The tool:

1. Pins the exact release and upstream commits, original source archive SHA-256, six reviewed public blob IDs and the original failed-log digest.
2. Verifies upstream/release blobs independently of the working tree's HEAD; checks the original scanner object's and working file's digests.
3. Reads the unchanged scanner's filename policy constants through Python AST, without importing or altering its implementation.
4. Builds the expected inventory from `git archive` of the pinned commit, respecting committed CRLF attributes, then compares every file byte and permission mode. Rejects unexpected/missing files, directories, duplicate paths, traversal, links and special members; never extracts the submitted archive.
5. Admits `.hlsl` only at the four exact reviewed paths with the exact public blob identities. Also verifies the three complete matched public spans by offset, raw length and SHA-256. No general HLSL exception or game-derived control is introduced.
6. Preserves the original combined failure as evidence. It cannot accept future content or new findings automatically and cannot publish anything.

This is a **release-specific provenance audit**, not a general detector for arbitrary future source trees. Its pass means the exact reviewed archive is unchanged and every recorded finding is accounted for. It does not replace a binary scan or certify unrelated upstream content.

Run from the audit checkout (fetch the two pinned commits if using a shallow clone):

```sh
python3 -m unittest discover -s tests -p source_archive_audit_test.py -v
python3 tools/release/audit_macos2_source.py /path/to/gtav-recomp-macos-source-v0.3.1-macos.2.tar.gz --repository /path/to/audit-checkout --original-scan /path/to/original-combined-scan.txt
```

**Final results:** 13/13 tests passed. Coverage includes generated shader injection even with public helper bytes, `.rpf`/`.spv` files, game payloads renamed as `.cpp`, replacement of an existing innocent text file, modified content at an approved HLSL path, changed template spans, missing/duplicate members, traversal and symlinks. Fixtures are synthetic; no copyrighted game shader is included.

Actual unchanged release archive: `PASS: exact macos.2 source provenance/inventory; 261 files; seven findings resolved`. The audit also prints that the original combined scan remains failed and publication requires review.

## Independent binary gate and original/final verdicts

| Gate | Result |
|---|---|
| Original unmodified combined scanner | **FAIL: 7 problem(s)**, preserved exactly. Four filename and three byte findings enumerated above. |
| Final source provenance audit | **PASS**, exact 261-file archive; all seven original findings technically resolved; no unexplained positive. |
| Original unmodified binary-only scanner | **PASS: 0 failing**, 62 files compared against 382 MB; longest remaining non-generic match 56 bytes. Original passing result preserved and bound to the unchanged ZIP. |
| Regression tests for source gate | **13/13 PASS**, including prohibited payload rejection. |
| GitHub release state | Draft/prerelease, `published_at = null`, target still the exact release commit. **No publication performed.** |

The binary scan was not rerun against installed files: no binary, scanner, control, ZIP or comparison-input correction was needed. Its original log digest and unchanged payload hashes preserve the existing passing gate. Any future binary change requires that gate to run again.

## Release asset integrity

Recomputed every local asset hash and compared it with the original recorded checksums and GitHub's current asset digest, size and uploaded state. All six agree. The checksum file itself also matches its previously recorded digest.

| Asset | Unchanged SHA-256 |
|---|---|
| Update ZIP | `0f9c48e0934e78a96752fa0918c681f209c65a78159831961acce8a0ca2a1551` |
| Downstream source archive | `543c374dfeaa9737dd5ba5c08a09beecc1f45f7cf5bdff482e477090db3fbcd1` |
| `VALIDATION.txt` | `37a63dee39712a69cb3dbe304381ae53af068c423c65d8e68c91f6dd5a296df0` |
| `SOURCE_INSTRUCTIONS.txt` | `8f11240be1f38e4384d8123ec50f9f4db7a2f9803a09d731efb23a8d75e401da` |
| `GAMEPLAY_CHECKS.md` | `d45e0ae5326bba0a871909d6310b9e0af80daf1cf57e11848f4d22513192015c` |
| `SHA256SUMS` | `58504da5ff12d4a1046a229b4b6fd682b5a3012c8211adae97c17aa502757a14` |

No asset replacement is required. Audit tooling and this report are a separate review supplement; they do not change the archived release source or executable build. The old validation asset remains historical evidence and should be read with this supplement.

## Remaining review

No technical source finding remains unresolved. A human must review and accept this evidence and the separate source-gate policy before the publication hold is lifted. The release remains [an unpublished draft](https://github.com/gsel001/gtav-recomp-macos/releases/tag/untagged-cae553a1741079823219). No GitHub assets or release notes were changed during this audit.

Previously outstanding gameplay checks (loading stutter, VSync transitions and fullscreen desktop-swipe freezes) are unchanged; this audit supplies no new gameplay or performance validation. It made no renderer changes and did not start or restart the game.
