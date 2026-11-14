# API

`include/spo.h` has the full API. Call everything from the GL thread, except
`spo_sample`.

## Lifecycle

1. With the GL context current, call `spo_init`. The library gets GL functions
   only through `get_proc_address`. A null `config_path` keeps settings in
   memory.
2. Register pages, options, counters and indicators. This closes at the first
   `spo_frame`.
3. Pass every input event to the overlay before the game.
4. Draw the game, call `spo_frame(width, height)`, then swap. Keep doing this
   while the menu is open.
5. Call `spo_config_save()` when `spo_config_dirty()` is set, then
   `spo_shutdown()` while the context is still current. If the context was
   lost, call `spo_context_lost()` before shutdown.

## Settings

Up to 8 pages of 8 options each: toggles, choice lists (up to 16 entries),
integer sliders and action buttons. Each page is a tab in the menu. Keys use `a-z`, `0-9`, `_` and `-`, at
most 32 characters. Labels are at most 24 bytes.

A saved value replaces the default when the option is registered. Your
`on_change` callback runs at registration and on every change. Return an
error to reject the value.

## Counters and indicators

Up to 8 of each.
- Counters show a number with 0 to 3 decimals. NaN hides the counter.
- Indicators show on or off.

## Input

- Guide pressed and released alone opens the menu. Pressing another button or
  moving a stick while Guide is held passes it through to the firmware. A stick
  that rests off centre does not block the menu.
- In the menu: up and down move, left and right change a value, South toggles
  or runs the row, East and Guide close the menu. L1 and R1 switch tabs and
  wrap around.
- The menu saves when it closes if a setting changed and `config_path` is set.
  A save error shows in the menu.
- While the menu is open, `spo_input_captured()` is true and the overlay
  consumes every event. Buttons still held when it closes stay captured until
  released.
- `on_pause` runs when the menu opens and closes. Clear held game input
  there; pausing the game is up to you.

Without SDL, use `spo_handle_input`, `spo_handle_axis` and `spo_input_sync`.

## Sampling

On Linux, a nice-19 thread reads `/proc` and `/sys` once a second. The render
thread uses the latest values without waiting. With
`SPO_INIT_NO_SAMPLER_THREAD` there is no thread: call `spo_sample()` from your
own, and stop before `spo_shutdown()`.

GPU load comes from the devfreq `load` file. Without one, it comes from the Mali
GPU_ACTIVE hardware counter (kbase job manager driver 11.x, `/dev/mali0`), read once a
second while the item is shown; if the counter reader cannot start, the value shows "Not available".

## Overlay items

Each value is its own toggle and sits in a fixed column, so rows line up. A row
shows when one of its values is on. Rows follow MangoHud's order; the graph sits
under the FPS row.

| Row | Values |
| --- | --- |
| GPU | `gpu_load`, `gpu_clock` |
| CPU | `system_cpu` (load of all cores), `cpu_clock`, `temperature` |
| RAM | `ram` (system RAM in use, MB) |
| PROC | `rss` (the game's resident memory, MB) |
| BAT | `battery`, `charging` |
| one per port counter and indicator | its key |
| FPS | `fps`, `frame_time`, `low_1` |
| graph | `graph` |
| OVERLAY | `self_cost` |

GPU load needs a kernel file for it, which the H700 kernels (muOS, KNULLI) do
not have. There the Show tab lists it as not available and skips it. The old
`process_cpu` and `mem_available` keys are ignored.

Presets: Off, FPS, Basic (FPS and frame time), Graph (adds the graph, CPU load
and GPU load) and Full (everything available). `spo_overlay_get` reports the
preset that matches the toggles, or `custom` when none does; `spo_overlay_set`
takes `items` and derives both.

## Config file

```ini
[spo]
format=1
preset=3
custom=0
anchor=top_left
font_scale=2
background_opacity=65
text_opacity=100
graph_samples=120
fps_low=30
fps_high=60

[spo.items]
fps=1
frame_time=1
graph=1
charging=0

[spo.counters]
objects=1

[port.graphics]
vsync=1
```

`[spo.items]` applies when `custom=1`. A `charging` key that is missing follows
`battery`. Counters and indicators have their own `[spo.counters]` and
`[spo.indicators]` sections, keyed by the key you registered.

Saves are atomic: a temp file is written, then renamed over the old one. Your
port creates the directory.

`font_scale` picks the font: 1 is 6x12, 2 is 8x16 (the default), 3 is 12x24 and
4 is 16x32. If the overlay does not fit the screen it uses the next smaller one.

## Colours

The colours are MangoHud's.

- FPS is red below `fps_low`, yellow below `fps_high` and green from there.
  Rates within 2% of a threshold count as reaching it, so 59.94 is green. The
  defaults are 30 and 60. `spo_target_fps_set(t)` sets them to t/2 and t; 0
  restores the values from the ini.
- A colour change shows only after the new colour has held for a second. The
  colour starts white for the first second.
- Frame time and 1% low are not coloured.
- The graph is one colour. Its scale is twice the frame time at `fps_high`,
  and a line marks that frame time.
- CPU and GPU load turn yellow from 60% and red from 90%. Temperature, RAM
  and battery are not coloured.

## GL state

Everything is created in `spo_init`: one buffer, one shader and a 256×256
atlas. The atlas holds one font size and the controller glyphs of the menu,
drawn at that size into the cells the font leaves free. It is uploaded again
when the size changes. Each frame saves the state below, draws once and restores it:
- program, texture unit 0 and its sampler;
- buffers, VAO and vertex attributes; on ES3, uniform buffer binding 0;
- blend state, enables, viewport and color mask;
- unpack row length, skips and buffer.

The overlay draws into the bound framebuffer. End transform feedback,
conditional rendering and timer queries before `spo_frame`.
