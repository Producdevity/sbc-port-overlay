# sbc-port-overlay

A C11 settings menu and performance overlay for native Linux ARM handheld
ports. One C ABI supports C, C++, Zig and Rust. The core produces a fixed
vertex list; the GLES backend supports GLES2 and GLES3 contexts. GL functions come
from the host's resolver. The base libraries link only the platform C/math
runtime; SDL2 is an optional adapter.

The approved [design](docs/design.md) defines the UI and behavior.
[API details](docs/api.md) cover ownership, callbacks, persistence and input.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/install"
```

Outputs: `libspo.a`, the platform shared library, `spo.h`, optional `spo.hpp`,
and CMake targets `spo::static` / `spo::shared`. No downloads occur at build
or runtime. `-DSPO_BUILD_EXAMPLES=ON` adds the SDL2 demo, adapter, and C/C++
smoke examples. `-DSPO_BUILD_SDL2=ON` builds only the optional `spo::sdl2`
adapter; `SPO_SDL_LINK_SHARED=ON` selects its shared-library variant.

For a port, `add_subdirectory(sbc-port-overlay)` and link the chosen target.
Installed consumers use `find_package(spo CONFIG REQUIRED)`.
The optional adapter uses `find_package(spo CONFIG REQUIRED COMPONENTS sdl2)`;
base-library consumers do not need SDL2 or pkg-config.

## C integration

Create the GL context first. Initialize and register settings before the
first presented frame; retain the context until shutdown.

```c
#include <SDL.h>
#include <spo.h>

static void *resolve(const char *name, void *user) {
    (void)user;
    return SDL_GL_GetProcAddress(name);
}

spo_config config = {0};
config.struct_size = sizeof(config);
config.abi_version = SPO_ABI_VERSION;
config.get_proc_address = resolve;
config.config_path = "overlay.ini";
config.port_name = "Example port";
config.port_version = "1.0";
/* config.on_pause clears held game input and chooses whether to pause. */
spo_result result = spo_init(&config);
```

In the existing event loop, process `spo_handle_sdl_event(&event)` before
game input and forward only unconsumed events. Gate polled controller state
with `spo_input_captured()`. Open newly added SDL controllers before passing
their device-add events to the helper so it can copy their initial state.
Bind the presentation framebuffer, draw the game,
call `spo_frame(drawable_width, drawable_height)`, then swap. Keep rendering
and event pumping while the menu pauses the game. Save dirty settings using
`spo_config_save()` at a safe point after menu close and before shutdown.
Config I/O never happens inside `spo_frame` or shutdown.

Non-SDL hosts feed positional button/axis events to `spo_handle_input` and
`spo_handle_axis`. Guide opens on a clean press/release; Menu chords pass
through while closed. The menu consumes all controller input, including held
release tails after closing. Start and Select alone have no menu action.

## C++, Zig and Rust

The optional C++ wrapper only owns successful initialization and shutdown:

```cpp
#include <spo.hpp>
spo::session overlay(config);
if (overlay.result() != SPO_OK) return overlay.result();
/* Use the C API. Destroy overlay while the GL context is still current. */
```

Zig imports the public header directly:

```zig
const spo = @cImport({ @cInclude("spo.h"); });
if (spo.spo_init(&config) != spo.SPO_OK) return error.OverlayInit;
defer spo.spo_shutdown();
```

Build the standalone smoke consumer against the static library:

```sh
zig build-exe examples/zig/main.zig -I include build/libspo.a -lc -lm -lpthread
```

Rust uses `#[repr(C)]` structs and handwritten declarations:

```rust
unsafe extern "C" {
    fn spo_init(config: *const SpoConfig) -> i32;
    fn spo_frame(width: u32, height: u32) -> i32;
    fn spo_shutdown();
}
```

[The Rust example](examples/rust/src/main.rs) includes the complete config
layout and an offline build script, with no binding generator or dependencies:

```sh
SPO_LIB_DIR="$PWD/build" cargo run --offline --manifest-path examples/rust/Cargo.toml
# Add SPO_SHARED=1 to exercise the shared library.
```

The smoke examples use explicit `SPO_INIT_NO_RENDER | SPO_INIT_NO_SAMPLER_THREAD`
mode and call `spo_sample()` once. Actual rendering
is demonstrated by `spo_demo`, which resolves GL through SDL and runs a steady
shader load. Its menu has port settings; it never initializes SDL audio.

## Overlay

Presets: 0 Off; 1 FPS; 2 FPS/frame time; 3 graph and system CPU/GPU load;
4 all available system metrics, registered counters/indicators, and self cost.
Every item is independently switchable. Unavailable metrics disappear.
Settings include nine anchors, integer font scale, separate background/text
opacity and graph length. Oversized layouts use the largest integer scale
that fits, preserving every enabled item. A drawable too small for 1x returns
an error without drawing.

FPS and mean frame time use adjacent windows of at least 0.5 s. Graph bars
are individual raw submitted-present intervals. These describe host present
cadence, including VSync/limiting; they do not measure scanout or GPU time.
Optional 1% low uses the slowest 1% of the last 512 intervals, after 120 samples.

System readers discover/open files at init. On Linux, default initialization
starts one internal nice-19 `SCHED_OTHER` sampler, with bounded `pread` at most
once per second per source. Shutdown joins it. Render frames read an atomic
snapshot without locks or file I/O. No heap allocation or GL storage creation
occurs per frame. Self cost includes both render CPU and amortized sampler CPU;
`S` identifies the sampler's contribution.

Hosts that prohibit internal threads set `SPO_INIT_NO_SAMPLER_THREAD` and call
`spo_sample()` from a host worker. Stop/join those calls before shutdown. All
other integration stays the same. See [both modes](docs/api.md#sampling-modes).

## Cost and verification

H700 acceptance targets: **<0.5% of one core averaged**, **<150 us CPU/frame**,
**<200 us GPU/frame**, zero frame allocations. The approved sampling mode uses one low-priority
internal thread, whose CPU counts toward the total. At 60 fps the CPU percentage target permits only 83.3 us/frame.
The current implementation misses these targets.

Measured on RG35XX-H / muOS 2606.0, CPU fixed at 1.512 GHz, native Mali-G31,
640×480, light shader workload paced at 60 submitted presents/s:

| Preset | Render mean / max us | Sampler core % | Total core % | GPU A/B estimate us |
| --- | --- | --- | --- | --- |
| Off | 8.6 / 25.0 | 0.0040 | 0.0558 | Baseline |
| FPS | 62.7 / 156.4 | 0.0043 | 0.3807 | 27.1 |
| Timing | 69.8 / 406.0 | 0.0044 | 0.4230 | 34.2 |
| Graph | 75.3 / 485.8 | 0.0387 | 0.4908 | 118.3 |
| Full | 87.8 / 514.3 | 0.0714 | 0.5984 | 222.9 |

Full's sampler used 7.155 ms of thread CPU over 10.026 seconds, with ten
passes and eighty reads. Its CPU cost is included above and in Self. No render
frame performs telemetry reads or takes sampler locks. Full's CPU p95/p99
were 160/290 us; cold/warm-up peaks are retained separately in the evidence.
The strict per-call ceiling, combined Full CPU limit and GPU target remain
unmet. Mali's empty queries include deferred scene work, so the GPU column
is an interleaved A/B estimate. See [raw data and limits](docs/verification.md).

Host core tests, sanitizer checks, four language consumers, ARM64 cross-build,
and Mesa GLES2/GLES3 state/pixel tests have been exercised. Device costs,
device input, gl4es and firmware coverage are recorded in the
[verification record](docs/verification.md). Host software rendering is not
evidence of handheld performance. No VPK/PortMaster archive or release is
produced by the library build.

## License

GPL-3.0-or-later. The embedded Spleen 5x8 font is BSD-2-Clause; its original
license, pinned source and conversion provenance are in `vendor/spleen/`.
No game assets or reference-port source code are included.
