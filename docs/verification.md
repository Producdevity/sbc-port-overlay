<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Verification record

**The performance acceptance gate remains unmet.** The approved sampler removes
telemetry I/O from render frames. Full now averages 87.8 us on the render thread
plus 0.071% of one core for sampling. Combined cost is 0.598% of one core;
render peaks and the GPU estimate also exceed their limits. Implementation and
the design amendment remain uncommitted.

## Current muOS measurements

Target: RG35XX-H, muOS 2606.0 Andromeda (`db2a3bd0`), aarch64, kernel
4.9.170, Mali-G31 proprietary r20p0 GLES 3.2, 640×480. CPU was temporarily
fixed at 1.512 GHz. GPU retained `simple_ondemand`; before/after readings were
360 MHz. CPU limits and thermal protection were unchanged. The original
`schedutil` governor was restored after every run.

| Preset | Render mean / p95 / max us | Sampler core % | Total core % | GPU A/B estimate us |
| --- | --- | --- | --- | --- |
| Off, first phase | 8.6 / 13.9 / 25.0 | 0.0040 | 0.0558 | Baseline |
| FPS | 62.7 / 116.9 / 156.4 | 0.0043 | 0.3807 | 27.1 |
| Timing | 69.8 / 127.2 / 406.0 | 0.0044 | 0.4230 | 34.2 |
| Graph | 75.3 / 133.9 / 485.8 | 0.0387 | 0.4908 | 118.3 |
| Full | 87.8 / 160.1 / 514.3 | 0.0714 | 0.5984 | 222.9 |

Targets remain **<150 us per render call**, **<0.5% total CPU on one core** and
**<200 us GPU per frame**. Means alone do not meet the render-call ceiling.
Full's p99 was 289.5 us; its separate warm-up maximum was 442.3 us. Other
warm-up peaks are retained in the CSV. No timing samples are clamped or
subtracted from the displayed frame cadence.

Full's sampler consumed **7,155.245 us of actual thread CPU over 10.025664 s**,
or about **714 us/s**, **0.0714% of one core**, and **11.90 us per presented
frame** at 60 fps. It completed ten passes and eighty source reads. Every
phase verified threaded mode, nice 19 and `SCHED_OTHER` (policy 0). FPS/Timing
perform no telemetry reads; the worker's wake/publication cost is still counted.

- [Raw current log](evidence/muos-sampler-2026-11-14/60fps.log)
- [Per-phase render and sampler costs](evidence/muos-sampler-2026-11-14/60fps.csv)
- [Target, artifact hash and immediate cleanup receipt](evidence/muos-sampler-2026-11-14/target.txt)

The continuous suite ran Off / FPS / Off / Timing / Off / Graph / Off /
Full / Off. Each phase lasted twelve seconds, with a separately reported
two-second warm-up and roughly 600 measured calls. Captures and instrumented
profiling were separate runs. Runs were under ten minutes, under the shared
device lock, with at least two minutes between runs.

Render measurements include external timer-call overhead. CPU percentage uses
measured thread CPU divided by elapsed time. Sampler percentage uses matching
published cumulative CPU and elapsed deltas; its publication can lag by one
pass. Total adds these independently measured rates. Full's observed process
CPU increase against adjacent Off phases was 0.59 percentage points, including
driver work outside the measured library calls.

All phases maintained 60 submitted presents/s with the demo's explicit limiter.
The driver reports VSync enabled but submits about 115 fps on a light scene
without that limiter. Submitted cadence is not panel scanout.

Mali's empty elapsed queries include deferred host-scene work. GPU estimates
subtract the mean of the two adjacent Off phases. They are A/B estimates,
not isolated overlay timers. A single `/proc/stat` read measured about 350 us
in an earlier diagnostic; those reads now run exclusively in the sampler.
Separate profiling finds remaining peaks in text geometry updates and GL
submission. It is not substituted for the uninstrumented results above.

The older synchronous implementation's [raw log](evidence/muos-2026-11-14/60fps.log)
and [heavier-workload results](evidence/muos-2026-11-14/heavy-workload.csv)
are retained as historical evidence. They do not describe the current sampler.

## Rendering and input

Current preset/menu captures are retained locally under
`build-device/muos-sampler-captures/`: `captures/preset-0.png` through
`captures/preset-4.png`, and `captures/menu-0.png` through `captures/menu-2.png`.
`index.html` is the gallery. Captures use renderer readback, not a camera or
scanout capture, and are excluded from timing. All eight were inspected for
legibility and clipping.

The installed muOS frontend loop includes mixer-reset/unmute commands. Tests
instead suspend the existing idle frontend and resume the same process. SDL
configuration comes from inspected firmware data without sourcing its global
controller/cache mutation function. These are isolated framebuffer diagnostics,
not normal PortMaster frontend-launch acceptance tests.

An earlier input run using installed `evemu-event` on the measured `muOS-Keys`
node observed Start/Select alone doing nothing, Guide opening on release,
D-pad navigation, South confirmation, East back/close, and held-release capture.
No volume keys or Guide chords were injected. Chords, disconnects and other
input boundaries have host coverage; physical button positions were not
manually verified. The local log is `build-device/muos-input/`.

## Audio and cleanup

Saved and live volume remained **0**. No audio-control commands were run.
Immediate volume/mute and complete ALSA mixer comparisons passed after each
sampler benchmark. Cleanup removed owned demo files, resumed the original
frontend and restored the CPU governor. The delayed final audit verified volume
0, the original frontend PID running, `schedutil`, no test process or owned
files, and the same ALSA mixer hash. The expired owned cooldown stamp was
removed under the lock; see the [final cleanup receipt](evidence/muos-sampler-2026-11-14/final-cleanup.txt).

After the earlier input run, a later check found the PipeWire mute flag had
cleared while volume remained 0 and the ALSA mixer hash was unchanged. Its
cause is unconfirmed. No audio controls were used to change it. No further
input events were injected. The harness now requires an exclusive grab of
SDL's existing evdev client before injection; that mitigation is compiled
but has not been device-tested. It does not change the earlier input evidence.

## Host and import checks

- AppleClang 21: core, SDL adapter and Python tooling tests pass.
- Linux: core, sampler lifecycle/concurrency, SDL and tooling tests pass.
- AddressSanitizer/UndefinedBehaviorSanitizer and ThreadSanitizer: core,
  sampler and tooling tests pass.
- Mesa 22.3 EGL: GLES2, GLES2 without VAOs, and GLES3 state/pixel tests pass,
  including graph bars/spikes, typed generic attributes, uniform-buffer base
  and range bindings, and pixels across a batch boundary.
- gl4es `8b1a3da5d2331d0f88b70be25963182d46eed857` renders the current
  sampler/graph demo under llvmpipe. This is host compatibility evidence only.
- C/C++, Zig 0.16 and Rust 1.97 consumers run locally; Rust links static/shared.
  Installed base CMake imports work without pkg-config; optional SDL component
  imports also work. ARM64 static/shared libraries and the demo cross-build.
- Source/storage audit: exactly one default worker, no frame-time heap or GL
  storage creation, and a threadless opt-out. Sampler tests intercept reads
  and mutex calls and keep render frames running while a worker read blocks.
  Shutdown/lifecycle tests verify joined workers and descriptor cleanup.

## Remaining coverage

The performance limits above remain required and unmet. Telemetry blocking is
resolved; render-update/submission peaks, Full's combined CPU cost and GPU cost
remain implementation work.

KNULLI H700 and Spruce RK3566 raw fixtures are retained under `tests/fixtures/`.
The user explicitly left AmberELEC RK3326 verification pending after SSH timed
out. KNULLI testing was authorized, but another draw-cost benchmark owned its
lock. No files were deployed there. Normal KNULLI launch, runtime costs,
screenshots and cleanup remain unverified.
