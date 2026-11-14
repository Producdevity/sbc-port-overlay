# API reference

The C ABI lives in `include/spo.h`. Every public struct starts with
`struct_size`, and `spo_config` also carries `SPO_ABI_VERSION`, so fields can
be added later without breaking old ports.

All calls except `spo_sample` must come from your GL thread. If a worker
thread produces a counter value, pass it to the GL thread yourself.

## Lifecycle

1. Make your GLES 2 or 3 context current, fill a zeroed `spo_config` and call
   `spo_init`. The library gets GL functions only through your
   `get_proc_address`. Leave `config_path` null to keep settings in memory
   only.
2. Register your pages, options, counters and indicators. Registration closes
   at the first `spo_frame`. The registration calls copy every string you pass
   in. Pointers you give as callback user data must stay valid until shutdown.
3. Pass controller events to the library before the game sees them (see
   [Input](#input)).
4. Each frame: draw the game into the framebuffer you present, call
   `spo_frame(width, height)` with the drawable size in pixels, then swap.
   Keep calling it while the menu is open, even if the game is paused.
5. Save with `spo_config_save()` when `spo_config_dirty()` is set: after the
   menu closes, or on exit. If a save fails, don't retry it every frame.
6. Call `spo_shutdown()` while the same context is still current. To switch
   contexts, shut down and initialise again on the new one. If the context is
   lost, call `spo_context_lost()` first, so shutdown skips the GL cleanup.

`SPO_INIT_NO_RENDER` runs everything except GL, for unit tests and bindings.

## Settings

A port can register up to 8 pages, each with up to 8 options:

| Type | Value |
| --- | --- |
| Toggle | 0 or 1 |
| Choice | index into the choice list (up to 16 entries) |
| Slider | integer between min and max, in steps |
| Action | a button; it has no stored value |

Keys are 1 to 32 characters: lowercase letters, digits, `_` and `-`. Option
keys only need to be unique within their page. Labels may be up to 24 bytes
and help text up to 80. Characters outside the font show as `?`.

When you register an option, a saved value replaces the default and your
`on_change` callback runs once. Later changes from the menu call it again:
return `SPO_OK` to accept the value or an error to keep the old one. Keep the
callback fast and queue any heavy work. If you reorder a choice list, migrate
the saved index yourself.

## Counters and indicators

Up to 8 counters and 8 indicators show port values in the overlay.
- Counters take a finite double with 0 to 3 decimals. Set NaN to hide a
  counter.
- An indicator appears once you first set it on or off.

Numbers on screen refresh twice a second. The frame-time graph updates every
frame.

`spo_preset_set` and `spo_overlay_get`/`spo_overlay_set` change the overlay
from code: item mask, position, scale, opacity and graph length. Changing
items by hand turns the preset into Custom.

## Input

Buttons are named by position: South, East, West and North. In SDL2, A is
South and B is East.
- **Opening:** the menu opens on a Guide press and release with nothing else
  pressed in between. A Guide chord, such as Menu+Start for the firmware's exit
  hotkey, passes through untouched. Start or Select on their own never open
  the menu.
- **While open:** the menu takes all controller input. Up/Down move, Left/Right
  change a value, South confirms and East goes back or closes. Move to the page
  title with Up, then use Left/Right to switch pages.
- **Closing:** after the menu closes, buttons that are still held stay
  captured until they're released, so the game doesn't see a stray press.
- **Analog:** sticks and triggers count as pressed past 20% and released
  below 15%. Sticks don't navigate the menu.
- **Controllers:** up to four controllers are tracked at a time. Disconnecting
  one, or losing focus, clears its state.

The SDL helper `spo_handle_sdl_event` never opens devices. Open a new
controller yourself, then pass its device-added event, so the helper can read
its current state. Without SDL:
- call `spo_handle_input` and `spo_handle_axis`;
- call `spo_input_sync` once when a controller connects;
- `spo_input_reset` clears one controller.

`on_pause` runs once each time the menu opens or closes. Release any held
game input there; whether the game pauses is up to you. The library never
touches audio or firmware hotkeys.

## Threads and sampling

On Linux, `spo_init` starts one sampler thread at nice 19 with the normal
scheduler policy. It opens the `/proc` and `/sys` sources once, then reads
each at most once a second with `pread`. Values are published atomically. The
render thread never waits for the sampler: if a read is in progress, it shows
the previous values. Values older than 1.5 s are hidden.

If your port can't have library-owned threads, set
`SPO_INIT_NO_SAMPLER_THREAD` and call `spo_sample()` from a thread of your own,
about once a second. It may block on file reads, so keep it off the render
thread. Stop calling it before `spo_shutdown()`.

## Config file

The INI file has one section for the overlay, two for its items and one per
port page:

```ini
[spo]
format=1
preset=3
custom=1
anchor=top_left
font_scale=2
background_opacity=65
text_opacity=100
graph_samples=120

[spo.items]
fps=1
frame_time=1
graph=1
system_cpu=1
gpu_load=1

[spo.counters]
draws=1

[spo.indicators]
shaders=1

[port.graphics]
vsync=1
render_scale=100
```

- **Unknown keys** are kept when the file is saved; comments and formatting
  are not.
- **Saving** writes a temp file, syncs it and renames it over the old one, so
  a failed save never leaves a half-written file.
- **The directory:** your port creates it. Only one process should use a file
  at a time.
- **Shutdown** never saves on its own.

## Statistics

`spo_get_stats` returns the overlay's own render cost: mean and max CPU time
per frame, draw count and vertex count. `spo_get_sampling_stats` adds the
sampler's CPU time, read count and scheduling policy.
- The Self overlay item shows render plus sampler CPU time per frame. `S` is
  the sampler's share.
- Frame times are measured when frames are submitted, not when they reach the
  screen.

## GL state

The overlay uses one vertex buffer, one shader and one 128×64 texture atlas,
all created in `spo_init`. Each frame it saves the GL state it touches, draws
once and restores that state:
- program and texture binding on unit 0, including the ES3 sampler;
- array buffer, VAO and vertex attributes; on ES3, uniform buffer binding 0;
- blend equations and functions;
- the depth, stencil, cull, scissor, blend and sample-coverage enables, and the ES3 rasterizer controls;
- viewport and color mask;
- unpack row length and skips, and the unpack buffer binding.

The overlay draws into whatever framebuffer is bound when you call
`spo_frame`. It never calls `glFinish`, reads pixels back or polls for GL
errors.

Before calling `spo_frame`, end any transform feedback or conditional
rendering. If you time the game with GL queries, end them first, or the
overlay's draw is included.
