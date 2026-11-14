# sbc-port-overlay

A settings menu and performance overlay for native Linux ports on ARM
handhelds running muOS, KNULLI, ROCKNIX, Spruce, AmberELEC and similar
firmware.

Tap the Menu button and a settings screen opens over the game. Players pick
an overlay preset, from a plain FPS counter up to a frame-time graph, CPU and
GPU load, clocks, temperature and battery. They can also change any settings
your port adds. Everything is saved per port.

It's a small C11 library with a C ABI, so ports written in C, C++, Zig or Rust
can use it. It draws with GLES 2 or 3 in your port's own GL context and stays
out of the way:
- the whole overlay is a single draw call;
- nothing is allocated per frame;
- system stats are read on a low-priority background thread, never on the
  render thread.

## Features

- **Menu.** Opens on a clean tap of Menu (SDL's Guide button), so firmware
  hotkeys such as Menu+Start keep working. Use the D-pad to move, South to
  confirm and East to go back. While the menu is open, the game gets no input.
- **Presets.** Off; FPS; FPS and frame time; adds the graph and CPU/GPU load;
  Full. You can also turn each item on or off yourself.
- **Layout.** Nine screen positions, integer text scaling for sharp text from
  640×480 to 1280×720, separate background and text opacity, and adjustable
  graph length.
- **Metrics:**
  - fps (averaged over 0.5 s), frame time, the frame-time graph and an
    optional 1% low;
  - system and process CPU load, GPU load and clock, CPU clock;
  - SoC temperature, battery and memory.

  Anything the device doesn't report is hidden.
- **Port settings.** Add your own pages with toggles, choice lists, sliders
  and action buttons. Values are saved to an INI file, and your callback runs
  when one changes.
- **Port counters.** Show your own numbers, such as draw calls or a "shaders
  compiling" indicator, as overlay items.
- **Self cost.** An optional item shows the overlay's own CPU time per frame,
  so anyone can check what it costs.

## Quick start

The example uses C with SDL2. The port keeps its own window, GL context and
event loop.

```c
#include <SDL.h>
#include <spo.h>

static void *resolve(const char *name, void *user) {
    (void)user;
    return SDL_GL_GetProcAddress(name);
}

static void on_pause(uint32_t open, void *user) {
    /* Release held game input; pause the game here if you want to. */
}

/* After the GL context is created and current: */
spo_config config = {0};
config.struct_size = sizeof(config);
config.abi_version = SPO_ABI_VERSION;
config.get_proc_address = resolve;
config.config_path = "conf/overlay.ini";
config.port_name = "My Port";
config.port_version = "1.0";
config.on_pause = on_pause;
if (spo_init(&config) != SPO_OK)
    fprintf(stderr, "overlay: %d\n", spo_last_error());

/* Event loop: the overlay sees every event first. */
SDL_Event event;
while (SDL_PollEvent(&event)) {
    if (spo_handle_sdl_event(&event))
        continue;
    handle_game_event(&event);
}
if (!spo_input_captured())
    poll_game_controller();

/* Draw the game, then the overlay, then swap. */
render_game();
spo_frame(drawable_width, drawable_height);
SDL_GL_SwapWindow(window);

/* On exit, while the context is still current: */
if (spo_config_dirty())
    spo_config_save();
spo_shutdown();
```

For ports without SDL, use `spo_handle_input` and `spo_handle_axis`
instead. [docs/api.md](docs/api.md) covers registering settings and
counters, input details, threading and GL state.

## Building

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

You can also add it to a port as a subdirectory:

```cmake
set(SPO_BUILD_SDL2 ON)
add_subdirectory(third_party/sbc-port-overlay)
target_link_libraries(my_port PRIVATE spo::static spo::sdl2)
```

Targets and options:

| | |
| --- | --- |
| `spo::static`, `spo::shared` | The library. It needs only libc, libm and pthreads. |
| `SPO_BUILD_SDL2=ON` | Builds the optional SDL2 helper (`spo_handle_sdl_event`). |
| `SPO_BUILD_EXAMPLES=ON` | Builds the SDL2 demo and the C/C++ examples. |
| `SPO_BUILD_GL_TESTS=ON` | Builds the GLES state tests (Linux with Mesa). |

The library never links libGLESv2 or EGL itself. It gets every GL function
through the resolver you pass in, so it works the same with the vendor
driver, Mesa or gl4es.

## C++, Zig and Rust

C++ can use the RAII wrapper in `spo.hpp`:

```cpp
#include <spo.hpp>

spo::session overlay(config);   // shuts down when it goes out of scope
```

Zig imports the header directly:

```zig
const spo = @cImport(@cInclude("spo.h"));
```

Rust declares the functions it needs in an `extern "C"` block.
[examples/rust](examples/rust) has the full config layout and a build script
with no dependencies.

## Background thread

On Linux, the library starts one sampler thread at nice 19. It reads
`/proc` and `/sys` at most once a second and publishes the values without
locks. If your port can't have library-owned threads, pass
`SPO_INIT_NO_SAMPLER_THREAD` and call `spo_sample()` from a thread of your own.

## Performance

Targets on an H700 handheld (4× Cortex-A53 at 1.5 GHz, Mali-G31), with the
Full preset:

| Budget | Target |
| --- | --- |
| CPU, averaged | under 0.5% of one core |
| Render-thread time per frame | under 150 µs, mean and p99 |
| GPU time per frame | under 0.2 ms |

`tools/run_knulli.sh` measures each preset on a device against an
overlay-off baseline.

## Related

To port, package and test games on these handhelds with an AI coding agent,
see the
[handheld development skill](https://github.com/Producdevity/sbc-handheld-development-skill).

## License

GPL-3.0-or-later. The embedded Spleen 5×8 font is BSD-2-Clause; see
`vendor/spleen/`.
