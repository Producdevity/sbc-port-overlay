# Contributing

- Per frame: one draw call, no allocations, no file reads, no waiting on the
  sampler thread.
- Any GL state the backend touches is saved and restored. Add new state to
  `tests/test_gl.c`.
- Keep GL and SDL types out of `spo.h`.

## Tests

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

The GLES tests need Mesa, so they run in Docker:

```sh
docker build -f tools/Dockerfile -t spo-dev .
docker run --rm -v "$PWD:/work" spo-dev sh -c \
  'cmake -S . -B build-linux -G Ninja -DSPO_BUILD_GL_TESTS=ON && cmake --build build-linux && ctest --test-dir build-linux'
```

Run the sampler tests under ThreadSanitizer when you change the sampler.

## On a device

`SPO_SSH_HOST=<host> tools/run_knulli.sh suite` measures each preset against
an overlay-off run.
