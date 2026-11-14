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
integer sliders and action buttons. Keys use `a-z`, `0-9`, `_` and `-`, at
most 32 characters. Labels are at most 24 bytes.

A saved value replaces the default when the option is registered. Your
`on_change` callback runs at registration and on every change. Return an
error to reject the value.

## Counters and indicators

Up to 8 of each.
- Counters show a number with 0 to 3 decimals. NaN hides the counter.
- Indicators show on or off.

## Input

- Guide pressed and released alone opens the menu. Guide with another button
  passes through to the firmware.
- In the menu: the D-pad navigates, South confirms, East goes back. Up on
  the title, then Left/Right, switches pages.
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

## Config file

```ini
[spo]
format=1
preset=3
anchor=top_left
font_scale=2
background_opacity=65
text_opacity=100
graph_samples=120

[spo.items]
fps=1
frame_time=1
graph=1

[port.graphics]
vsync=1
```

Saves are atomic: a temp file is written, then renamed over the old one. Your
port creates the directory.

## GL state

Everything is created in `spo_init`: one buffer, one shader and a 128×64
atlas. Each frame saves the state below, draws once and restores it:
- program, texture unit 0 and its sampler;
- buffers, VAO and vertex attributes; on ES3, uniform buffer binding 0;
- blend state, enables, viewport and color mask;
- unpack row length, skips and buffer.

The overlay draws into the bound framebuffer. End transform feedback,
conditional rendering and timer queries before `spo_frame`.
