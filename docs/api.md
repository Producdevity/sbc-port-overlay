<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Integration contract

`include/spo.h` is the authoritative C ABI. Public structs use fixed-width
fields, start with `struct_size`, and config includes `SPO_ABI_VERSION`.
All calls except manual `spo_sample` are serialized on the host's existing GL
thread. Port workers hand counter values through their host's synchronization.
Callbacks never unwind or reenter the library. Getters return borrowed
version strings or copied structs. Registration copies strings/choice text;
callback user pointers remain host-owned until shutdown.

## Lifecycle

1. Make the intended GLES2/3 context current, fill zeroed `spo_config`, and
   call `spo_init`. Pass the host's resolver; the library never loads EGL/GL
   itself. A null config path means in-memory preferences.
2. Register pages/options/counters/indicators. Registration freezes at the
   first `spo_frame`. Duplicate keys, invalid ranges, excessive strings and
   capacity overflow return errors. Never retain stack pointers as callback
   user data. Labels are bounded UTF-8 bytes; unsupported glyphs render `?`.
3. Feed all controller events before game input. Clear game-held controls
   when capture starts; gate polled state while `spo_input_captured()` is true.
4. Bind the intended presentation framebuffer, draw the game, call
   `spo_frame` once per actual presentation, then swap. Pass drawable pixels.
   Continue frames/events while simulation is paused.
5. At a safe point after menu close and before exit, save dirty config.
   Handle errors without retrying file I/O every frame.
6. Shut down while the same context is current. Shutdown signals and joins the
   internal sampler before closing its descriptors. For context replacement,
   shut down first and reinit on the new context. If it is lost, call
   `spo_context_lost()` before shutdown to abandon GL handles safely.

`SPO_INIT_NO_RENDER` explicitly disables GL creation/submission for headless
unit tests and ABI consumers. It still builds geometry and samples enabled
metrics. It is not a graphics compatibility test or a GPU benchmark.

## Sampling modes

On Linux, default initialization starts exactly one internal sampler thread.
It uses normal `SCHED_OTHER`, priority 0, and nice 19. Initialization verifies
that policy without changing the calling thread's priority. A thread or policy
setup failure returns `SPO_THREAD_UNAVAILABLE`; there is no synchronous fallback.
The non-Linux host smoke examples explicitly select manual mode.

Discovery opens proc/sysfs sources once during initialization. The sampler uses
bounded `pread` calls, at most once per second per source, for enabled telemetry.
It publishes atomic 32-bit words under a sequence counter. The render thread
makes one bounded snapshot attempt and retains the previous snapshot if a write
overlaps. It takes no sampler locks, waits for no reader, and performs no file
reads. Values older than 1.5 seconds are hidden. A heavily loaded host can delay
this low-priority thread; stale data is preferable to blocking presentation.

Hosts that forbid library-created threads set:

```c
config.flags |= SPO_INIT_NO_SAMPLER_THREAD;
```

Then call `spo_sample()` from any host-selected thread, typically once per
second. More frequent calls do not increase source polling beyond one Hz.
This call can block in kernel file reads; keep it off the render thread to
preserve that thread's budget. Concurrent manual calls return `SPO_BUSY` for
the overlapping caller. Calling it in default threaded mode returns
`SPO_INVALID`. The sampling call does not run callbacks or change the GL
thread's last-error state. Stop and join host sampling calls **before** calling
`spo_shutdown`; initialization/shutdown must not race any API call.

The rest of port integration is identical in both modes. No sampling call is
required in the default mode. Neither mode allocates heap memory per frame or
per sampling pass.

## Settings and values

Eight port pages, eight options per page, eight counters and eight indicators.
Keys are 1..32 ASCII lowercase letters, digits, `_` or `-`; option keys are
scoped by page. Page labels and option/choice labels permit 24 bytes, help
80 bytes, choices 16 entries. Counter labels permit 10 bytes and units 4;
indicators permit 16 label bytes. The public header's result codes distinguish
invalid input, capacity, registration closure, I/O, GL and callback failures.

Toggle values are 0/1; choices use zero-based indexes. Integer bounds are
inclusive, step is positive and endpoints/default are aligned. Invalid
programmatic values are rejected. Menu steps clamp sliders and wrap choices.
Actions have keys for identity and are never invoked by loading preferences.

On registration, a valid saved value replaces the default and calls
`on_change(id,value,user)` once. A rejected initial callback fails registration.
Later callbacks accept with `SPO_OK` or reject without changing the stored
value. Queue expensive host work rather than blocking in the callback.
Reordering choices requires a host config migration.

Counters accept finite doubles, with 0..3 decimal places; NaN marks a counter
unavailable and infinity is rejected. Indicators become available on their
first On/Off set. Numeric text refreshes at most twice per second; raw frame
history updates every frame. Counter/indicator IDs belong to their respective
API namespaces, not the port-option namespace.

`spo_preset_set` replaces item defaults. `spo_overlay_get/set` permit host
tools to inspect/change the item bit mask, anchor, scale, opacity and graph
length. Item bits are `1u << SPO_FPS`, etc. Changing items sets Custom.
Counter/indicator visibility is individually adjustable in their menu pages.

## Input

Buttons use physical positions. SDL2 A is South and B is East. Feed mapped
controller events once; do not feed duplicate joystick button events to a
second game-input route. Open/close uses Guide down/up with no button or
active axis before/between the edges. Chord cancellation lasts until Guide
release. Start/Select alone do nothing. While closed, chord events are not
delayed, replayed or swallowed by the library.

While open, all controller input is consumed. East navigates back or closes a
root page; South confirms. Focus the header using Up to select root pages
with Left/Right. Closing drains held buttons/axes until neutral. Event
consumption cannot undo game state already forwarded earlier, so the host's
pause handler must clear held gameplay input, and polling must honor capture.

Analog input uses signed -32768..32767 sticks and 0..32767 triggers. Activation
at 20% / release at 15% provides chord/drain hysteresis; sticks never navigate
menus. Track at most four simultaneously active controllers. Removal/focus
loss resets held/candidate/repeat state. The SDL helper opens no devices:
the host owns discovery, connection and controller lifetime.

Open an added SDL controller before passing its device-add event to the
helper; it copies the already-open controller's cached neutral/held state.
Remapping also resynchronizes that state. Non-SDL hosts can call
`spo_input_sync(id, button_bits, axes)` once on connection, without generating
menu actions. A held Guide in that snapshot cannot become a release-only
menu click. A null axes pointer means all axes are neutral.

`on_pause` reports menu visibility once per transition; release-drain can
outlive visibility. The host chooses simulation/audio behavior. No library
code writes audio controls or modifies firmware hotkeys/mappings.

## Persistence and statistics

INI keys are documented in the design. Settings use `[spo]`, `[spo.items]`,
`[spo.counters]`, `[spo.indicators]`, and `[port.PAGE_KEY]`. Unknown keys survive
saves within the fixed 256-entry/64 KiB limit; comments/formatting do not.
Unsupported file-format versions and oversized files fail initialization.
Malformed known preferences use defaults with an error status. Only the host
creates the destination directory; one process owns each config file.

Explicit saves write a unique sibling temp file, fsync, rename, then fsync the
directory where supported. Failed saves retain dirty state and the last
complete file. A null config path makes save clear the in-memory dirty flag.
Shutdown never implicitly writes. `spo_last_error` supplies the last error;
the host chooses its own logging.

`spo_get_stats` copies CPU mean/max, elapsed mean, sample count and previous
draw/vertex/read counts. Thread CPU excludes time descheduled or blocked;
elapsed time includes those waits. External measurement includes timestamp
overhead. `spo_get_sampling_stats` additionally reports cumulative sampler CPU nanoseconds,
elapsed nanoseconds, cycles, reads, maximum cycle CPU, scheduling policy, and
sampling/combined CPU rates. Counters are published once per pass, so they can
lag by one sampling interval. Compare CPU deltas against the corresponding
published elapsed deltas, rather than a different wall-clock window.

Default mode accounts the sampler's thread CPU clock, including wakeups and
publication. Manual mode accounts CPU spent inside successful `spo_sample`
calls, excluding the host worker's other work. The optional Self item shows
combined render-plus-sampler CPU us per presented frame, `S` for the sampler's
contribution, and previous draw count. Sampler cost is amortized using observed
present cadence. Until a rate is available, it shows `S--`. Sampling runs at one
Hz; render means cover at least 0.5 seconds, so these are recent estimates with
different window boundaries. Self cost excludes GPU execution and driver work
on other threads. Present timestamps describe submitted cadence, not scanout.

## Renderer state

The backend uses one private geometry buffer with three preallocated regions,
one shader program, and one 128×64 nearest-filtered atlas. Every enabled preset
and menu submits exactly one draw; Off submits none. The fixed capacity is
1,024 quads, sufficient for all supported registrations and menu layouts.
There is no per-frame resource creation, shader compilation, `glFinish`,
readback, or error polling.

Initialization compiles the shader, uploads the font and allocates all storage.
It also submits glyph/graph shader paths into a temporary private 1×1 framebuffer
and waits for completion. That warm-up never draws into the host framebuffer.
Initialization restores both ES3 draw/read framebuffer bindings and the
renderbuffer binding, in addition to normal submission state. Initialize outside
active queries or transform feedback; initialization can block.

Closed-overlay text refreshes at most twice per second. Identical text retains
its geometry and uploaded storage. A geometry change uploads a coalesced changed
span into the next region. Ordinary graph updates replace only the newest
1×1 RGBA texel in an unused atlas row when its raster value changes, with a
scrolling offset uniform. Identical raster values need no upload. History
reset, scale or graph-range changes refresh the 120×1 row. No old columns are
expanded or shifted on the CPU. Raw present intervals remain in the core's
512-entry history; only graph raster heights are quantized to physical pixels.

The state audit includes program, buffer/VAO/attribute state, texture unit 0
binding/sampler, blend equations/functions/enables, depth/stencil/cull enables,
scissor enable, viewport, color mask, sample coverage, and ES3 raster controls.
Atlas uploads also preserve unpack row length/skips and ES3 unpack-PBO binding.
Upload pitches already meet alignment requirements. Dither, scissor box, PACK
state, host texture parameters and texture unit 1 remain untouched. Draw calls
use the framebuffer the host has bound.

ES3 pulls four packed 32-bit words per quad from a single 16 KiB uniform block
using vertex ID, preserving every typed current attribute. It never queries or
sets those values: their numeric type is not queryable, and querying through a
different type is undefined in the [ES specification](https://registry.khronos.org/OpenGL/specs/es/3.2/es_spec_3.2.pdf).
Uniform-buffer binding 0, its range offset/size, and the independent generic
binding are restored. A zero queried size restores `glBindBufferBase`; nonzero
ranges restore `glBindBufferRange`. ES2 uses the same retained geometry through
vertex arrays, with its own VAO when available. Its fallback restores every
changed array descriptor; both paths restore floating-point current attributes.

End/pause transform feedback and end conditional rendering before calling.
Host queries must exclude the overlay if they should measure only the game.
The library cannot undo query results. Call on a complete intended color target.
