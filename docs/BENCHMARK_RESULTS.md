# M1 shader contention: results and review hold

This is diagnostic/benchmark work, not a verified performance optimization.
Keep the pull request DRAFT. Do not merge it as an FPS/stutter or artifact fix.
The review branch makes Nice +5 opt-in (false by default on every platform),
retains two compiler workers on desktop Mac and preserves demand promotion.
Reporting is off by default. No direct-spawn path is installed in the renderer.

## Recorded synthetic experiment

Machine: fanless M1 MacBook Air,8GB RAM,60Hz display. Measured protocol commit
3e2ec0b99bd02639a284e81e6230e6e8e61b6692; analyzed at
 afa903c4fadad08edbda0223a53ce345e0a8fa68. The later default-off change did not
regenerate or alter the original dataset. All A/B trials selected priority
explicitly. Eight45-second trials per case, ABBA/BAAB (four per arm),40 trials,
1800 measured seconds. Release helper -O2; no sanitizer performance timings.

A=inherited nice0, B=nice5, verified in actual children. Baseline instead toggles
sample collection on/off without compiler children. Two deterministic CPU workers
at10/50/100% duty. Sustained and DXC cases add three nice0 competitors, at most
six CPU-heavy workers including30Hz foreground. Fixed calibrated integer work;
bounded samples flushed after trials. These are host response timings, not GTA
FPS or display cadence. Real DXC compiled24 original authored shader shapes with
production-like flags; all3943 started outputs passed spirv-val. No game shader,
Xenos translation, Vulkan pipeline creation or Metal compilation was benchmarked.

| Case | Mean trial p99 A ms | Mean trial p99 B ms | Paired B−A ms | Exploratory95% interval ms |
|---|---:|---:|---:|---:|
| cpu_light | 17.645 | 17.991 | 0.346 | [-1.251, 1.942] |
| cpu_burst | 17.166 | 17.344 | 0.178 | [-0.643, 0.998] |
| cpu_sustained | 19.048 | 21.745 | 2.696 | [-5.463, 10.856] |
| dxc_mixed | 19.403 | 29.221 | 9.818 | [-10.507, 30.144] |

Mean trial p99 is a trial-level summary, not a pooled overall p99. Four adjacent
pairs/case is exploratory; approximate t intervals assume independent pair
effects and all include zero. DXC throughput A11.444 vs B10.417 jobs/s (~9% lower),
paired delta−1.028 jobs/s, interval[−2.774,+0.718]. Child wall p95 averaged225.159
vs255.463ms. No demonstrated benefit. Two slow late B trials remain included;
no measured trial was excluded or repeated to select a favorable outcome.

Measured launch slots stayed95.9–96.6% occupied. Two compiler threads did not imply
two concurrent system() children on this host. The DXC harness models two queues
sharing ONE launch mutex; the independent-process CPU case is a different model.
Queue latency, drain tails, failures and peaks are in
[trials.csv](benchmarks/m1-2026-11-05/trials.csv); all paired effects in
[summary.json](benchmarks/m1-2026-11-05/summary.json). Original raw synthetic timing
samples remain local and were independently reconciled:48443 samples,40 complete
trials,3943 validated outputs and7 reserved-but-unlaunched jobs at cutoff.

Controls: mean foreground CPU15.145946s with collection vs15.123759s without;
variation exceeds that0.022187s difference. It does not prove zero overhead.
Sampled synthetic thermal state was nominal/on AC, but system swap-used snapshots
varied2969.25–4241.88M in sysctl units. Background processes, DVFS and memory
activity were uncontrolled; attribution to niceness is not established.

Pilots: first failed the>=80% occupied-slot gate (~78.6%); increased corpus
complexity without lowering the gate. Second passed (~88%, two-slot preparation
model). Third passed (~96%, final serialized model). Pilots overlapped preparation
and are not performance comparisons. An initial fragile subprocess-overlap
assertion failed; it was replaced by a production thread-count check after a
separate system() probe confirmed serialization. A real injected300ms stall was
detected as318.623ms response, plus skipped slots. Failed evidence remains local;
it was not relabeled as passing or added to release archives.

## Exploratory real-game follow-up

30FPS/native VSync/audio on, Nice0, original caches retained. The player drove
from a home garage in a white car, mostly right turns, then repeated the loop.
Exact roads/traffic/time and start latency were uncontrolled. Two120s captures:

| Metric | First measured traversal | Warmed traversal |
|---|---:|---:|
| Five-second CPU report windows |24|24|
| Weighted mean CPU-present gap ms |33.536|33.565|
| Worst reported gap ms |332.16|146.38|
| Reported gaps>50ms |35|39|
| Native frames with pipeline-related lost draws |88|0|
| Background shader pending/completed/failed |0/0/0|0/0/0|

The player reported improvement when warmed, but the mixed tail counts do not
establish a broad objective improvement. No global p99/display FPS is available.
Fair thermal state1 and substantial system-wide paging were sampled. External
queries averaged89.6/104.4ms wall per five-second poll (not measured CPU cost),
with no instrumentation-off gameplay control. No Nice+5 comparison was warranted
without background compiler activity. Pipeline compilation is a distinct stage
and remains a better next correlation target than DXC priority on these routes.

Brief blue square artifacts across much of the image occurred roughly once per
two minutes while driving, NOT throughout the drive. They persisted after the
original executable/signature was restored; root cause is unresolved. No burst
frame/timestamp was obtained. Cache state, native pipelines and memory activity
are not isolated by code rollback. No speculative rendering/cache changes were
made. Shooting/rotation/fullscreen/VSync-transition tests were deferred.

The initial diagnostic invocation appended duplicate cap/VSync options; CLI11
rejected the command and no GPU plugin loaded. That startup is invalid and
excluded. After restart consent, only unique reporter/priority flags were passed
and unchanged saved30FPS/native-VSync values loaded correctly. All diagnostic
code/signature was subsequently rolled back with consent; current progress and
caches were preserved, temporary reports off. No private game log/save/cache or
generated runtime file is included in this PR.

## Standalone launch probe

spawn_probe.cpp is an owned-fixture investigation, never called by production.
Eight grouped semantic checks passed locally: literal arguments/environment,
redirection, niceness without changing parent priority, exit/signal status,
missing/nonexecutable tools, bounded timeout and recovery. At most two tiny
children; direct child intervals overlapped (~0.213s for two200ms sleeps),
system() intervals did not (~0.446s). Peak direct child RSS1753088bytes. This is
one architecture observation, not a timing assertion/CI performance threshold
or evidence that two real DXC jobs are safe with8GB unified memory.

[Apple libc system.c](https://github.com/apple-oss-distributions/Libc/blob/main/stdlib/FreeBSD/system.c)
holds a mutex around spawn/wait in its UNIX03 path, consistent with the probe;
matching this public source to the exact installed libc build is unverified.
Future integration must review signal/cancellation/wait ownership, environment,
shader filenames/output rename and failure/cache semantics. The probe does not
exercise those production workflows. Any initial experiment should retain ONE
launch slot to separate direct-launch overhead from concurrency. No direct-spawn
performance change is proposed for merge here.

## Review status

Earlier local Release/ASan/UBSan policy groups5/5 and Python analysis/pairing8/8
passed at the measured source. The review branch adds explicit default-off
assertions and a Mac standalone-probe CI target; its CI is separate evidence.
The default-off review branch is not rebuilt/installed as a game release. The
macos.2 draft remains held for separate final human review; existing archives,
checksums, taint_scan.py, its failure evidence and audit working files are intact.
