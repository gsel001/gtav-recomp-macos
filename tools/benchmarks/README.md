# Mac shader contention benchmark

This harness measures host scheduling and authored DXC shader compilation, not
GTA FPS, displayed frame cadence or the full game shader pipeline. It does not
start GTA or modify installed game files. The recorded measurements use protocol
3e2ec0b and production experiment 8ab2228. The later review branch keeps Nice +5
opt-in by default; no recorded timing data was regenerated for that change.

Build on Mac with Xcode command-line tools:

```sh
clang++ -std=c++23 -O2 -Wall -Wextra -Werror tools/benchmarks/worker.mm -framework Foundation -o /tmp/gtav-bench-worker
python3 -m unittest discover -s tools/benchmarks -p test_analyze.py -v
python3 tools/benchmarks/run.py --worker /tmp/gtav-bench-worker --dxc /path/to/dxc --validator /path/to/spirv-val --out /tmp/gtav-benchmark-pilot --pilot
python3 tools/benchmarks/run.py --worker /tmp/gtav-bench-worker --dxc /path/to/dxc --validator /path/to/spirv-val --out /tmp/gtav-benchmark-measured
python3 tools/benchmarks/verify_results.py /tmp/gtav-benchmark-measured
python3 tools/benchmarks/results.py /tmp/gtav-benchmark-measured /tmp/gtav-benchmark-analysis
```

Use an environment allowing niceness changes; the tool sandbox can silently keep
nice 0. The runner verifies actual compiler/CPU child nice values. Run CTest with
at most two build jobs; Release timings only. Do not run builds during measured
trials. Stop active gameplay only with consent; the runner refuses heavy trials
if it sees gtav_fast and checks again during each trial. It kills only its owned
process groups on timeout/cancel. It requires nominal thermal state and normal
power mode to begin, waits up to ten minutes for cooldown and aborts a trial for
serious/critical thermal state. Fair-state observations remain visible in data.

## Fixed measured protocol

Five cases: no-compilation instrumentation control, two CPU children at 10% duty,
two at 50% duty, two sustained plus three normal-priority competitors, and real
DXC compilation plus three competitors. Including the foreground, CPU-heavy
processes never exceed six. DXC has two work queues but serialized launches (see
below); it therefore uses fewer simultaneously active workers than the CPU case.

Each case has eight 45-second trials in ABBA/BAAB order. A inherits priority,
B uses nice +5. The no-compilation case instead compares raw sample collection
on/off with the same foreground work. Both forms still use clocks for pacing and
collect final process rusage; this measures incremental sample-buffer overhead,
not all possible production logging overhead. Ten seconds minimum between trials
plus thermal gating. No per-frame disk writes; samples flush at the end.

Calibrate one fixed integer-work iteration count toward 8 ms once before all
trials. Thirty-Hz monotonic deadlines; no catch-up bursts: skip elapsed slots and
record their count. Raw target/wake/finish timestamps are seconds since trial
start. Wake lateness, service, response and completion deadline overrun are separate
milliseconds. Deadline overrun count uses a >1 ms tolerance beyond 33.333 ms.
Percentiles use nearest rank on raw samples; never average window percentiles.

The shader generator is original and fixed-seed (20261105), with 24 vertex/pixel
program shapes and varied expression/texture/branch complexity. Each DXC job also
gets a deterministic job macro, identical for A/B job IDs. Sources and generator
are hashed. Every started job has a deadline; all successful output modules must
pass spirv-val. Two queues run a sustained stream, capped at 4,000 reservations.
Reserved jobs not launched at window end are recorded, not called successes.
Outputs are unique temporary paths and deleted after validation. These are real
DXC invocations, not an asserted driver-cache hit/cold-cache experiment. No guest
shader translation, Vulkan pipeline creation or Metal GPU work is performed.

The child niceness receipt and validation/meta writes occur per job, equally in
both arms. They add harness overhead, not foreground per-frame I/O. Per-job
rusage is collected by the launcher for its descendants; instant ps telemetry
samples owned launcher/foreground/competitor processes, not every short-lived
DXC child. Peak child memory is per-job descendant rusage. RSS is not unique
physical memory across processes. Aggregate child CPU/wall figures include spawn
helpers. Queue waiting and tool time are distinct.

## Pilots and discovered launch behavior

Pilot 1's light corpus compiled and validated but its measured occupied launch
slots were 78.6%, below the prespecified 80% activity gate. Increase source
complexity before measurement; do not lower the gate. Pilot 2's larger corpus
passed that gate (~88%). Pilots overlapped preparation/build work and are not
performance comparisons.

The production stress test confirmed two compiler threads but did not observe
two short XenosRecomp invocations overlapping. An independent owned test using
two C++ threads each calling system("sleep 1") took about 2.042 seconds on this
host. Thus multiple worker threads do not imply multiple concurrent child
commands on this Mac's libc path. Production uses std::system in Run(). No
production launch change was made.

The measured DXC harness models this observed behavior with a shared launch lock:
two queues contend for one child-command launch slot. The CPU stress case retains
two independent child workers for artificial scheduling pressure. This model is
not execution inside the complete game process. Its occupancy denominator is one
launch slot; this is distinct from pilot 1/2's two-slot model. Pilot 3 passed with
~96% slot occupancy after this change. All pilot outcomes are retained locally.
Do not equate a two-independent-process synthetic result with production's
serialized-launch behavior.

Analyzer unit tests include known timestamp stalls, invalid/missing samples and
failed trials. A real foreground run with an injected 300 ms sleep also detected
the >250 ms response and skipped slots. Mixed-outcome production stress submits
600 unique synthetic markers twice in three shuffled bursts and checks exact
cache/failure/success totals, queue drain, duplicate suppression, early repeated
demand promotion and two compiler threads plus intake. A timing-sensitive peak-
overlap assertion was replaced with a process thread-count check; transient
subprocess overlap is not a guaranteed scheduling property.

The final verifier reconciles all 40 trials, raw samples versus stored metrics,
non-overlapping foreground work, priority receipts, shader reservations/outputs,
validation status and occupied-slot thresholds. It verifies protocol consistency;
it cannot establish causal isolation from background applications or OS memory
activity. The result exporter includes per-trial queue latency and drain tails;
the pairing tests protect B-minus-A signs in both halves of ABBA/BAAB. The small
system-launch diagnostic is a preparation probe, not part of the measured matrix.

## Interpretation and bounds

Use independent trial-level paired effects and show all trials, thermal states,
power, memory pressure and swap observations. Four pairs per case is exploratory,
not enough to establish a general hardware performance guarantee. Predefined
tails and throughput are co-primary; report compiler-readiness regressions even
if foreground latency improves. The analyzer is not a CI speed threshold.

Record failed/aborted trials without treating partial data as a valid 45-second
result. Do not repeat runs to select a favorable answer. Preserve all game caches;
these synthetic outputs are isolated. Actual gameplay, installation and restarts
remain consent-gated at 30 FPS/native VSync/audio on. See the final result report
and gameplay route plan before any such action.
