# Isolated macOS shader compiler contention experiment

Based on `bbdf1da008a98db4390e5b50a08ff4f7bbde278d`. Adapted only compiler-child
priority from upstream `d44d0063c20271d5b1e4badfe2f6d81e10901da0` and the idea of
shader accounting from `a85c4f737e46eec6e3ef0113a0aefe8427a48287`. No long waits,
larger worker pools, renderer changes, FSR, mission commands or bug reports.

Desktop Mac retains two compiler workers even if hardware thread reporting changes.
Other platforms keep the previous worker formula. Draw-demand promotion of both
intake and compilation queues is unchanged, including requests before intake.

`gtav_native_shader_low_priority` defaults false on every platform. The initial
local experiment defaulted true on desktop Mac; the review branch removes that
default because the measured comparison did not establish a benefit.
POSIX compiler child commands use `/usr/bin/nice -n 5` when enabled, inherited
priority when false. Windows/iOS process paths are unchanged. Each shell argument
is quoted, including embedded apostrophes. This is CPU scheduling niceness, not a
memory budget or guarantee of Apple QoS/core selection. The option affects newly
launched children. Failure of a required compiler command remains a shader failure.

Reporting is off by default. For a consented test only, enable BOTH:

```toml
gtav_native_frame_report = true
gtav_native_shader_report = true
```

The existing CPU present summary is followed by one shader line every five seconds
of active presentation; there is no independent reporter thread or per-frame file.
If presentation freezes, the reports also stop; they cannot independently diagnose
a blocked submit thread. No names, collection sorting or extra mutex is used.

Counters update cheaply at unique acceptance and terminal transitions even with
reports disabled, enabling meaningful totals when reporting is turned on later.
Snapshots use relaxed atomics and may temporarily overlap concurrent transitions.
They are diagnostic, not a transactional accounting API. Definitions:

- pending: accepted shaders in intake, queue or compilation;
- completed: successful translation/compilation of a unique shader, not tool calls;
- failed: compiler/variant/output failure; the shader's draws remain unavailable;
- cached: accepted shaders whose base and required variants were already on disk;
- skipped: missing shaders saved while compilation is disabled, not compiler errors.

Duplicate creation does not increment counters; disk hits and disabled compilation
do not leak pending. Compare differences between adjacent totals. Five-second
reports cannot locate an individual hitch or prove causality. CPU present times
are not timestamps of frames reaching the screen.

## Synthetic validation

`cmake -S tests -B /tmp/gtav-shader-tests -DCMAKE_BUILD_TYPE=Release`, then build
with `-j 2` and run CTest. Existing timing/upload and 30 FPS hook tests remain.
New tests exercise worker configuration, all terminal counters, concurrent updates,
real child nice values and shell quoting. The production compiler is included in a
synthetic harness for intake, duplicate suppression, demand promotion, cached and
skipped states, failed child commands, successful fake compiler tools and generation
notification. Fake outputs contain only the word 'synthetic', not real shaders.

The process-priority test needs an environment permitting child niceness changes.
The execution sandbox silently retained nice 0; outside it, the owned test children
correctly ran at inherited nice 0 and nice 5. This does not verify game performance.

## Controlled comparison after installation/restart consent

Do not change the installed app or runtime, settings, caches or current gameplay
for build preparation. Keep all existing release files/checksums and audit work.

1. After explicit consent, back up the actual installed app's frontend, main
   launcher and code-signature resources before installation. Stage only this
   frontend into the app, sign locally and verify. The normal next launcher start
   synchronizes frontend code to the runtime. Keep backups for exact rollback;
   do not substitute a guessed baseline for the user's actual installed build.
2. Keep 30 FPS, native VSync on, resolution and other settings fixed. Use the SAME
   instrumented executable for A/B: low_priority=false versus true, with both
   reporting flags identical. Capture/dumps/memory reports remain off. Make only
   consented, backed-up configuration edits and restart between comparisons.
3. Record build hash, flag values, power/thermal condition, route and event times.
   First drive the same new-area route, then repeat warmed routes and shooting
   bursts. Do not delete caches to manufacture cold loads. Cache warming biases
   the first run, so alternate A/B order across sessions and report that limitation.
   A warmed route with no completed/pending activity cannot test compiler contention.
4. Compare p95/p99/worst CPU present gaps, >50 ms count, compiler completion pace,
   failures and visible missing effects. Lower priority can delay shader readiness.
   Note loading separately from steady-state play and keep private logs local.
5. Test fullscreen/swipes and VSync transitions separately from the initial fixed
   profile comparison. Only if a freeze occurs, take a short local process sample.
6. Restore reporting off and previous settings after the comparison. To roll back
   code, restore actual backed-up app frontend, launcher and signature, verify,
   and let the next consented launch sync that frontend. Never delete game data,
   saves or shader caches. The low_priority=false option supplies an immediate
   scheduling control, but exact code rollback uses the backup.

The initial local build was tested with inherited priority, then the original
installed code/signature was restored. Two exploratory driving captures showed
no new background compilation, occasional CPU-present gaps and intermittent blue
square artifacts that persisted after restoration. A warmed drive felt better;
the artifact frequency was approximately once per two minutes, not continuous.
See BENCHMARK_RESULTS.md for the synthetic results and limitations. No FPS,
stutter or fullscreen-freeze improvement is claimed. The default-off review
branch has not been installed or built as a release; keep this PR draft. The
macos.2 release still needs its separate final human review.
