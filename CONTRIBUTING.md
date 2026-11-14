# Contributing

## Ground rules

- **Language:** C11. The public API is the C ABI in `include/spo.h`. GL and
  SDL types stay out of the core and the public header.
- **Per frame:** no allocations, no file I/O, no locks shared with the
  sampler thread. Each enabled preset or menu page draws in exactly one call.
- **GL state:** any GL state the backend changes has to be saved and restored.
  Add it to the state list in [docs/api.md](docs/api.md#gl-state) and cover it
  in `tests/test_gl.c`.
- **Sampler:** keep it to one thread at nice 19, reading each source at most
  once a second. The `SPO_INIT_NO_SAMPLER_THREAD` mode has to keep working.
- **Source files:** start with an SPDX header. Comments explain only what the
  code can't, such as a spec rule or a driver quirk.

## Tests

Host tests:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

With AddressSanitizer and UBSan:

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined' \
  -DCMAKE_SHARED_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

If you change how the sampler publishes values or how it starts and stops, run
the sampler tests under ThreadSanitizer as well.

The GLES state tests run in a pinned Debian image with Mesa's surfaceless EGL.
They cover ES2 with and without VAOs, and ES3:

```sh
docker build -f tools/Dockerfile -t sbc-port-overlay-dev .
mkdir -p build-linux
docker run --rm --network none -v "$PWD:/work:ro" \
  -v "$PWD/build-linux:/work/build-linux" sbc-port-overlay-dev \
  sh -c 'cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DSPO_BUILD_GL_TESTS=ON -DSPO_BUILD_EXAMPLES=ON && cmake --build build-linux && ctest --test-dir build-linux --output-on-failure'
```

## Cross-compiling

Use your port's aarch64 toolchain file, or `cmake/zig-aarch64.cmake` with Zig
installed. The Zig toolchain targets glibc 2.17. Link the demo against the
target's SDL2, never the host's.

## Measuring on a device

`tools/run_knulli.sh` and `tools/run_muos.sh` copy the demo to a handheld,
run each preset against an overlay-off baseline and summarise the results.
Set `SPO_SSH_HOST` to the device's SSH host. Keep the screenshot runs separate
from the timing runs.

## Pull requests

- Keep build output and personal paths out of commits.
- Pin anything the build fetches.
