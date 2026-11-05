# Experimental macOS performance downstream

Based on andrewnakas/gtav-recomp at `2a76184`. This is an independent downstream,
not an official release or a native Metal conversion. Vulkan/Plume/MoltenVK remains
in use. Do not expect a guaranteed frame rate.

## Included changes

- Carry forward the Mac launcher fetch-resolve workaround, Retina window flag,
  opt-in Steady 45 FPS menu preset, and disable the unused SDK window presenter.
  Earlier local testing confirmed the Retina correction and steadier 45 FPS cap.
  Removing the hidden presenter removed its sampled main-thread drawable wait;
  elimination of all fullscreen freezes remains unconfirmed.
- Promote missing shaders actually requested by draws ahead of shaders merely
  created during loading. Promotion covers container intake and translation queues,
  including requests arriving before their containers. Running compiler processes
  are not interrupted. Existing pipeline warmup and asynchronous compilation remain.
- Reclaim trailing upload chunks unused for an entire 120-completion window per
  frame slot. Release happens only in FrameComplete after the GPU fence. Keep one
  chunk for reuse, and retain the window's highest demand to avoid allocation churn.
  Enabled by default on desktop Mac only. Mobile uniform descriptor caches have
  different lifetimes, so mobile rings are not trimmed.
- Add bounded, optional CPU present-call timing percentiles and partial cache
  memory accounting. No captures, network reports or per-frame disk writes.
- Release old swap-chain framebuffers before resize destroys their image views,
  including present-mode changes. This corrects resource destruction order; it is
  not yet proof that the reported blank/red picture is fixed.
- Avoid pipeline-cache snapshots while the previous background write is busy and
  preserve the pending-save count until a new snapshot is scheduled.

## Reference to 5★ Port

We inspected [5★ Port](https://github.com/kevinerikjs/gta5-macos-port), particularly
[MRRenderer.mm](https://github.com/kevinerikjs/gta5-macos-port/blob/main/app/Source/MetalRenderer/MRRenderer.mm)
and its [shader preparation](https://github.com/kevinerikjs/gta5-macos-port/blob/main/app/Scripts/build_metal_shaders.py).
Its presentation statistics, upload-ring ownership and memory accounting informed
these original implementations. No 5★ source, game shaders or assets were copied.
The existing GPL license and upstream credits are preserved.

Its renderer consumes a different game/command/shader interface. WGSL-to-Metal
shader conversion and its packed web-asset cache cannot be transplanted directly
into this Xbox 360 recomp. Upstream already implements shader warmup and buffer
reuse; this downstream extends them rather than adding duplicate systems.

## Controls

Set these in the existing gtav.toml before launching:

```toml
gtav_native_frame_report = true
gtav_native_memory_report = true
gtav_native_trim_uploads = true
```

Reports are disabled by default. Set trim_uploads false to compare retained upload
memory with the upstream behavior. The memory line reports cached texture bytes,
vertex/index buffers and reserved upload chunks, not process RSS or total GPU memory;
it excludes render targets, driver allocations and guest memory.

The frame line reports CPU present-call gaps and time inside the present call,
including lock contention. It does NOT report when a drawable reached the screen.
Samples are bounded to 1024 per reporting interval. Five-second percentiles help
separate pacing problems from an average FPS counter; they do not fix pacing on
their own. A desktop swipe or loading pause can be included as a long gap.

## Build and validate

Use the upstream source archive's matching SDK, Plume and XenosRecomp revisions,
apply the patches from this repository, and follow the upstream build instructions.
A normal GitHub clone alone does not include the SDK or dependencies. Keep the
installed game library and shaders local; rebuild only the frontend/launcher for
these changes. Upstream release downloads do not include this downstream's changes.

Dependency-free policy tests:

```sh
cmake -S tests -B build/policy-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/policy-tests
ctest --test-dir build/policy-tests --output-on-failure
```

These check percentiles, invalid input, bounded storage, empty windows, upload
retention across loading spikes and renewed demand. They also run in CI on Linux
and macOS. They do not replace a GPU/gameplay test.

Before calling a build gameplay-validated, test startup, fullscreen/resize and
VSync changes, then repeat the same shooting and driving route after warmup with
and without upload trimming. Compare timing tails and memory, not only average FPS.
The new shader priority, ring reclamation and resize change still need that test.
No game files or compiled game libraries are published here.

## Validation recorded on 2026-11-05

- Release-mode policy tests passed locally on Apple silicon.
- The same tests passed with AddressSanitizer and UndefinedBehaviorSanitizer.
- All frontend sources and Plume compiled and linked into a local gtav_fast,
  using the v0.3.0 source archive's patched dependencies and the installed v0.3.0
  runtime dylibs. Utility shaders were built from this project's authored HLSL.
  The SDK libraries were reused, not rebuilt. The local link harness is not a
  complete release package; no binary release is published.
- The macOS launcher built successfully with its normal CMake target.
- git diff --check passed.
- No gameplay run of the new binary has been performed, and the installed app
  was not replaced or restarted. Frame-rate improvements remain unmeasured.
