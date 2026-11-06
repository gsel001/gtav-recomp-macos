# Historical gameplay comparison plan

This plan predates the two exploratory driving captures. See
`docs/BENCHMARK_RESULTS.md` for executed results, rollback and unresolved artifacts.
The review branch keeps Nice +5 off by default. A later failed startup showed
that appending duplicate cap/VSync flags is rejected by CLI11; use unique extra
report flags with the existing saved profile instead. Do not run any installation
or gameplay action without current consent.


Status: NOT RUN—AWAITING CONSENT. The prepared executable remains experiment
8ab2228 / local artifact SHA-256 4ed7cb20408674a663e9778d4b74ea05bb79d20c01d8ba7cbb107fbba8cd4c0a.
Benchmark tooling did not modify production code and does not require a new game
executable. This plan neither installs nor starts it.

## Compatibility evidence

[Rockstar's PC command-line reference](https://support.rockstargames.com/articles/2VjbVziQCiTiiVhDbmnexc/full-list-of-command-line-parameters-for-grand-theft-auto-v-on-pc)
documents the benchmark and frame-time switches for the PC game. Their presence in
Xbox 360 TU27 is unverified. The inspected frontend/SDK does not implement those
PC switches. A textual search is not proof that the guest has no benchmark, but
there is no verified command/UI path available here. Do not create commandline.txt
or claim the official PC benchmark was used in this recompilation.

Local native A/B toggling changes a cvar every 60 frames; active compiler children
retain old priority. Do not use that mechanism as the primary compiler-priority
comparison. The Windows ab.ps1 also changes settings, hides/mutes/uncaps the game
and force-stops it; do not execute it unchanged on this Mac.

SDK input_script is supported by the source, but its controls and start-time must
be validated on this build. Use a non-destructive short input probe after consent.
Existing input_run_around is not a guaranteed driving route or game-state replay.
GTAV_IGNORE_SETTINGS prevents saved-option loading and TestSet persistence; if
used, explicitly supply and verify every required option. It does not stop game
autosaves, runtime output or cache writes.

## Preparation after explicit consent

Back up the actual installed app frontend, launcher and code signature. Inspect
current runtime/configuration and back up any file proposed for editing before
presenting the exact install/restart steps. Preserve saves and original caches;
do not assume replacing an app executable alone prevents game autosaves. A copied
test save/profile/cache is an option only if paths/support are verified and the
user separately approves it. No such copy or configuration change has been made.

Use 30 FPS, native VSync enabled and normal audio for both settings. Keep the same
instrumented binary, draw/shader policy, resolution, graphics options, controller,
power source and equivalent thermal condition. Set low_priority false for A and
true for B; restart between arms only with checkpoint-loss consent. Enable frame
and shader reports identically; captures/replays/dumps/memory reports remain off.
Confirm effective startup values from the local log rather than trusting launch
arguments that saved settings may override.

Choose an accessible common save/start location with the player; record it and
route waypoints before timing. No particular mission/save state is presumed.

## Three distinct routes

| Scenario | 120/90-second schedule | Repeatability requirements |
|---|---|---|
| Streaming drive, 120 s | 0–10 s stationary settle; 10–100 s follow a recorded road loop crossing at least two district/asset boundaries and a fast road entry; 100–120 s stop at the same endpoint. | Same vehicle, view, start/save, route and approximate speed. Record collisions, traffic obstructions, wanted level, time/weather changes and route deviations. Do not compare disrupted and unobstructed laps as equivalent. |
| Shooting/effects, 120 s | 0–15 s settle in a quiet fixed area; 15–75 s six repeatable bursts (same rounds/weapon/aim directions); 75–105 s a fixed small number of explosions only if equipment and safe test location are approved; 105–120 s settle. | Match weapon/effects count, camera, start position, NPC/wanted state and ammo. If explosions are unavailable, document a shooting-only route rather than improvising different effects per arm. |
| Camera rotation, 90 s | 0–15 s stationary; 15–75 s repeated slow complete rotations at a calibrated constant input; 75–90 s settle. | Same scene, view and controller input. Count rotations and record visible tearing, missing objects or freezes. Do not infer displayed cadence from CPU calls alone. |

These are route templates; first validate and record accessible exact waypoints
and control values after consent. Scripted input can reduce variation, but traffic,
physics, asset residency and game simulation remain nondeterministic.

Record first traversal separately from warmed repeats. Do not purge player or OS
caches. Without a validated, approved isolated cache/profile arrangement, matched
cold-cache gameplay A/B cannot be claimed. Counterbalance A/B sessions, repeat at
least four runs per setting where practical, and retain unfavorable/deviating runs
with their flags. If there is no shader activity, that run measures warmed gameplay
but cannot establish shader-compiler contention benefit.

## Metrics and separate presentation checks

Keep five-second CPU present-call p50/p95/p99/worst, >50 ms counts and changes in
shader totals alongside event timestamps and visual observations. Do not average
window p99 values into a run-wide p99. A future bounded raw game-timing buffer
would require its own source review and overhead check; it was not added here.

After the initial fixed-profile comparison, separately test fullscreen for one
minute, several desktop swipes, then another minute visibly on the game. Note
whether a visible freeze starts before a swipe and whether beginning the swipe
unlocks it while sound continues. Only during an observed freeze take a short
owned game-process sample. Reports can stop when the submit path blocks.

VSync off/on transitions are a separate functional test, only after consent to
temporarily leave the preferred profile. Record blank/red output, responsiveness
and tearing before/after settled states. Restore native VSync on and the 30 FPS
cap afterward. A synthetic scheduling improvement does not clear these checks.

## Exit and rollback

Restore reporting off and backed-up settings after approved trials. If needed,
restore the actual app code/signature backups, verify signing, and let the next
consented launcher start synchronize that frontend to the existing runtime.
Never remove game data, saves or shader caches as rollback. Logs/samples stay
local and are not attached to a public release. The macos.2 draft remains unpublished.
