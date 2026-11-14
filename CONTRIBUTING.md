<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Contributing

Use C11 with the public C ABI in `include/spo.h`. Keep GL/SDL types out of the
core and public header. New renderer operations need a state-audit entry and
a real-context isolation test. Preserve zero frame allocations, bounded storage,
one internal nice-19 SCHED_OTHER sampler and its threadless opt-out. Telemetry
reads stay off the render thread and run at most once per second per source.
Render frames must never wait for the sampler or take its locks. Add SPDX headers to
source/build scripts. Keep comments limited to non-obvious constraints.

Run the host tests and consumers described in README. For sanitizers:

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined' \
  -DCMAKE_SHARED_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

The optional Linux renderer suite uses EGL surfaceless Mesa, including ES2
with/without VAOs and ES3. `tools/Dockerfile` pins the Debian image and package
snapshot. Package changes occur only inside that build image.

Linux sampler tests check thread/descriptor cleanup, unchanged caller priority,
the worker's policy, manual-mode concurrency, source read rate, and render frames
while a sampler read is blocked. Run these under ThreadSanitizer when changing
publication or lifetime rules. A plain payload protected only by a sequence
counter would still be a C data race; keep the payload atomic.

```sh
docker build -f tools/Dockerfile -t sbc-port-overlay-dev:bookworm .
mkdir -p build-linux
docker run --rm --network none -v "$PWD:/work:ro" \
  -v "$PWD/build-linux:/work/build-linux" sbc-port-overlay-dev:bookworm \
  sh -c 'cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DSPO_BUILD_GL_TESTS=ON -DSPO_BUILD_EXAMPLES=ON && cmake --build build-linux && ctest --test-dir build-linux --output-on-failure'
```

For ARM64 Linux, pass the port's normal CMake toolchain or use
`cmake/zig-aarch64.cmake` with Zig installed. The supplied toolchain selects
glibc 2.17; SDL demo linking must use the target's SDL2 ABI, not a host library.
Do not ship SDL2, drivers or system libraries with the demo.

Fixture and device work follows the task's shared lock and cooldown rules.
Only use devices authorized for the current session. Every probe/run must acquire `/tmp/pm-device-lock`,
check for an existing game, remain below ten minutes and leave two minutes
between runs. Do not steal locks. Never write audio controls; launch only
when volume is already 0. Remove owned test artifacts and restore the frontend.
Use the installed firmware/PortMaster sources for launch behavior. For the
current KNULLI cost pass, leave all clock/governor controls untouched and do
not run mixer, mute or unmute commands. Use only a verified read-only volume
getter. MuOS is excluded from this pass.

Run captures separately from timing. Report CPU mean/p95/p99/max, GPU query
validity or A/B uncertainty, present rate, artifact hash and actual device
identity. Never replace unavailable device evidence with host results.

Keep personal/host paths, build products and downloaded runtime binaries out
of version control. Pin fetched inputs. Do not commit, push, deploy or create
a repository without the user's explicit authorization for that action.
