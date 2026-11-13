<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# sbc-port-overlay: proposed design

Review this design before implementation. Phase 1 delivers this document only.
All numbers in the mockups are illustrative. No device timings, screenshots,
firmware compatibility or performance limits have been verified yet.

## Scope and acceptance limits

C11 library for native Linux ARM ports: a controller settings menu and five
performance-overlay presets. One C header, optional C++ RAII header, CMake
static/shared targets, and an optional SDL2 adapter. The core has no SDL or GL
types. The GLES backend resolves functions through the host; it never links
libGLESv2, EGL, a window system, or a font library.

The H700 acceptance limits at 640x480, including the Full preset's self meter:

| Measure | Required limit | Meaning |
| --- | --- | --- |
| Average CPU | <0.5% of one core | Sum of library CPU time / elapsed wall time; never divide by four cores |
| Render-thread CPU | <150 us/frame | Entire `spo_frame`, including telemetry, geometry, GL queries/submission and state restoration |
| GPU | <200 us/frame | Incremental GPU cost; CPU submission time is a separate measure |
| Allocations | Zero per frame | Library heap and GL resource/storage creation finish before the first frame |
| Extra threads | Zero | Host serializes calls on its existing GL/input thread |

At 60 fps the average CPU allowance is **83.3 us/frame**; at 30 fps it is
166.7 us/frame, with the independent 150 us/frame limit still applying.
Measure average, p95, p99 and maximum; an average cannot establish a per-frame
limit. Init, registration, menu interaction, callbacks and config I/O get
separate timings. Include input/counter API CPU in the average overlay cost.

State queries, GL buffer reuse and synchronous sysfs reads are the main risks.
A bounded parser cannot bound a kernel read or a driver call. Meeting these
limits requires device evidence; failure must be reported rather than hidden
by skipping state restoration or dropping expensive frames from the report.

## Reference findings

Read for behavior and scale; no source, font or renderer code is copied:

| Reference | Source snapshot | Consequence for this design |
| --- | --- | --- |
| BotW port | `src/platform/input_map.cpp`, `botw-native/runtime/src/host/menu.cpp`; checkout `c96bb530d724de462bb3d96595520b29e91cfdf8` | Retain clean Guide release and grouped label/value pages. Track buttons already held before Guide too. |
| BotW controls | Same snapshot | Its East-confirm, South-back and Start-close behavior differs from this library's requested South-confirm, East-back and inert Start/Select. |
| BioShock port | `src/performance_overlay.zig`, `src/uikit/renderer.zig`; checkout `310d759e67e14fb3647938a95529d0b5b54c7c2c` | Retain stable numeric refresh and a graph updated every frame. Use a 0.5 s FPS window, configurable anchors, a single atlas/batch and tighter default geometry. |

These are source observations, not physical-device UX validation.

## Menu and navigation

At 640x480 the centered menu fits within 600x432 px. The menu uses 2x bitmap
text (10x16 px cells), clear row highlighting and at most ten visible rows.
At 1280x720 it uses 3x text. Menu text size is independent of overlay size.
Use opaque-enough menu surfaces for readability; overlay opacity never makes
the menu invisible. No animation, blur, mouse dependency or scrolling list.

Root pages: **Overlay**, each registered port group in registration order,
then **About**. The focused header's Left/Right arrows select root pages.
Up from the first row focuses the header; Down returns to the first row.
Long page names are shortened in the header; the selected name is shown in
full on a second line. Detail pages return to their parent with East.

| Control | Behavior while open |
| --- | --- |
| D-pad Up/Down | Move focus; clamp at header/last row |
| D-pad Left/Right | Change focused value, or root page when the header is focused |
| South | Toggle, advance a choice, activate a link/action, or enter/leave slider editing |
| East | Leave slider editing, return from detail, or close a root page |
| Clean Menu click | Close from any page |
| Start / Select | Consume; perform no action |
| Other controller input | Consume; perform no action |

Sliders change by their registered integer step and clamp at endpoints.
Slider editing keeps Up/Down from accidentally changing rows; East leaves
editing with the displayed value applied. Values apply immediately; East is
navigation, not Undo. Choice lists wrap. Only D-pad navigation/slider changes
repeat (300 ms initial delay, then 100 ms); toggles, choices via South and
actions require a new press. Opposite directions held together do not repeat.

### Overlay page

```text
+--------------------------------------------------------+
|                 < Overlay >                 1 / 4      |
|--------------------------------------------------------|
| > Preset                           < 3: Graph >        |
|   Items                                       >        |
|   Position                         < Top left >        |
|   Size                             < 2x >              |
|   Background opacity               < 65% >             |
|   Text opacity                     < 100% >            |
|   Graph length                     < 120 frames >      |
|                                                        |
| D-pad: move/change   South: select   East: close       |
+--------------------------------------------------------+
```

Items opens a short index. Frame items: FPS, frame time, graph, optional 1%
low. System items: system CPU, process CPU, GPU load, CPU clock, GPU clock,
SoC temperature, battery, RSS, MemAvailable. Port counters and indicators
each have their own detail page, up to eight entries each. Self cost is one
toggle covering CPU us/frame and actual draw calls. Empty port lists vanish.

```text
+--------------------------------------------------------+
| Overlay / Items                                        |
|--------------------------------------------------------|
| > Frames                                      >        |
|   System                                      >        |
|   Port counters                               >        |
|   Port indicators                             >        |
|   Self cost                         [ON]               |
|                                                        |
| South: select   East: back                             |
+--------------------------------------------------------+

+--------------------------------------------------------+
| Overlay / Items / System                               |
|--------------------------------------------------------|
| > System CPU                        [ON]               |
|   Process CPU                       [OFF]              |
|   GPU load                          [ON]               |
|   CPU clock                         [OFF]              |
|   GPU clock                         Unavailable        |
|   SoC temperature                   [OFF]              |
|   Battery                           [OFF]              |
|   Process RSS                       [OFF]              |
|   Available memory                  [OFF]              |
|                                                        |
| South: toggle   East: back                             |
+--------------------------------------------------------+
```

Every item is independently switchable. A missing source has an unavailable,
disabled menu row and occupies no overlay space. Its saved preference remains
intact. Available counters are separate toggle rows, named by the host.

### Port group page

One root page per registered group; at most eight options per group. Reject
oversized registration so the host can split it into named groups. Each group
can mix all four option types; no nested host-defined menus in version 1.

```text
+--------------------------------------------------------+
|                 < Graphics >                2 / 4      |
|--------------------------------------------------------|
| > VSync                             [ON]               |
|   Frame limit                       < 60 FPS >         |
|   Render scale                      < 100% >           |
|   Reload shaders                    [Run]              |
|                                                        |
| Render scale: 50..100, step 5                          |
| South: select/edit   East: close                       |
+--------------------------------------------------------+
```

The second registered group uses the same layout and has its own root page.
Use factual option labels and short help text. Actions run once per press;
the library supplies no destructive action and no system/audio controls.

### About page

```text
+--------------------------------------------------------+
|                  < About >                  4 / 4      |
|--------------------------------------------------------|
| sbc-port-overlay     0.1.0                             |
| Example Port         1.2.0                             |
| Renderer             GLES 2.0                          |
| License              GPL-3.0-or-later                  |
| Font                 Spleen 5x8 / BSD-2-Clause         |
|                                                        |
| Config               Saved                             |
|                                                        |
| East: close                                            |
+--------------------------------------------------------+
```

Config status can be Unsaved or Save failed. Show a concise error here and a
status line on the current page after a failure. Do not expose filesystem
paths in the normal UI. The host can retrieve an error code for its own log.

## Presets, anchors and overlay mockups

Fresh config defaults: Off, top-left, 2x font, background 65%, text 100%,
120 graph samples. Changing the preset replaces item switches with its
defaults. Changing any item then displays `Custom (from 3)`, for example.
Cycling a custom preset advances from its base number. Preset 0 clears all
switches; enabling an item while Off makes a visible custom overlay.
Position, size, opacity and graph length survive preset changes.

| Preset | Default items |
| --- | --- |
| 0 Off | None; no GL calls or periodic telemetry reads |
| 1 FPS | FPS |
| 2 Timing | FPS + average frame time |
| 3 Graph | Timing + raw frame-time graph + system CPU/GPU load |
| 4 Full | Graph + process CPU, clocks, temperature, battery, RSS, MemAvailable, every registered port counter/indicator, self cost |

1% low is individually selectable and off in presets. Implement it only if
the bounded calculation meets the same device CPU limits.

The following show **every preset in top-left and bottom-right positions**.
Outer frames represent the screen; inner boxes represent the overlay, not
literal box-drawing glyphs. All other pixels remain the game. Off is identical
at every anchor. Values, including self cost, are invented examples.

```text
0 Off / top-left                 0 Off / bottom-right
+---------------------------+    +---------------------------+
|                           |    |                           |
|                           |    |                           |
|            game           |    |            game           |
|                           |    |                           |
|                           |    |                           |
+---------------------------+    +---------------------------+

1 FPS / top-left                 1 FPS / bottom-right
+---------------------------+    +---------------------------+
| +----------+              |    |                           |
| | 59.9 FPS |              |    |                           |
| +----------+              |    |                           |
|                           |    |              +----------+ |
|                           |    |              | 59.9 FPS | |
|                           |    |              +----------+ |
+---------------------------+    +---------------------------+

2 Timing / top-left              2 Timing / bottom-right
+---------------------------+    +---------------------------+
| +-------------------+     |    |                           |
| | 59.9 FPS   16.7ms |     |    |                           |
| +-------------------+     |    |                           |
|                           |    |     +-------------------+ |
|                           |    |     | 59.9 FPS   16.7ms | |
|                           |    |     +-------------------+ |
+---------------------------+    +---------------------------+

3 Graph / top-left               3 Graph / bottom-right
+---------------------------+    +---------------------------+
| +-----------------------+ |    |                           |
| | 59.9 FPS   16.7ms     | |    |                           |
| | 33ms             |    | |    | +-----------------------+ |
| | 16ms _||_||_||||__||_ | |    | | 59.9 FPS   16.7ms     | |
| | CPU 42%   GPU 71%     | |    | | 33ms             |    | |
| +-----------------------+ |    | | 16ms _||_||_||||__||_ | |
|                           |    | | CPU 42%   GPU 71%     | |
|                           |    | +-----------------------+ |
+---------------------------+    +---------------------------+

4 Full / top-left                4 Full / bottom-right
+---------------------------+    +---------------------------+
| +-----------------------+ |    |                           |
| | 59.9 FPS   16.7ms     | |    |                           |
| | 33ms             |    | |    | +-----------------------+ |
| | 16ms _||_||_||||__||_ | |    | | 59.9 FPS   16.7ms     | |
| | CPU42% PRC108% GPU71% | |    | | 33ms             |    | |
| | CPU0 1512 GPU650 MHz  | |    | | 16ms _||_||_||||__||_ | |
| | SOC 54C  BAT 82% CHG  | |    | | CPU42% PRC108% GPU71% | |
| | RSS 128M AVAIL 392M   | |    | | CPU0 1512 GPU650 MHz  | |
| | DRAWS 812             | |    | | SOC 54C  BAT 82% CHG  | |
| | SHADERS ON            | |    | | RSS 128M AVAIL 392M   | |
| | SELF 63us  1 draw     | |    | | DRAWS 812             | |
| +-----------------------+ |    | | SHADERS ON            | |
|                           |    | | SELF 63us  1 draw     | |
|                           |    | +-----------------------+ |
+---------------------------+    +---------------------------+
```

Use white text, a dark neutral background and one muted graph accent. Mark
clipped graph spikes with a bright cap; color is never the only information.
No large FPS numeral, gauge, shadow, title bar or per-metric chart. Port names
and versions belong in About. `PRC` means process CPU; `M` is MiB in compact
rows. Selected-item help explains abbreviations and units.

Geometry uses integer physical pixels. Font cells are 5x8 at integer scale
1..4, row advance 9*scale, panel padding 3*scale, screen margin 8 px. Graph
width is 120*scale and height 20*scale. At 2x, FPS-only is approximately
92x28 px, Timing 182x28 px; graph panels are 252 px wide. Full with the two
example port items is approximately 200 px tall. Width depends on enabled
items, with reserved numeric columns to avoid size jitter as values change.
Long values use explicit units/overflow notation, never a false capped value.

Nine anchors: TL, TC, TR, CL, CC, CR, BL, BC, BR. With usable rectangle
`(m,m,W-2m,H-2m)`, align the panel's left/center/right and top/center/bottom;
center coordinates round down to integer pixels. Recompute on resize/items
change. The menu replaces the overlay while open.

Registration limits of eight counters and eight indicators keep Full within
640x480 at 2x, using at most 24 characters per row. If a requested scale cannot
fit all enabled items, use the largest smaller integer that fits and show
`4x requested / 2x fits` in Size. Never omit enabled metrics to fit. Restore
the requested scale when space permits. Reject a drawable too small even for
1x, return an error and leave host rendering untouched.

Opacity choices: background 0..100% in steps of 5; text 10..100% in steps of
5. Graph length: 30, 60, 90 or 120 presented-frame intervals, default 120.
The graph stays fixed-width and right-aligns shorter history; every bar is
one raw interval, with no averaging, interpolation or bucket reduction.
Label the graph's scale, initially 0..33.3 ms; expand to include larger values
at numeric-refresh boundaries, and retain a cap marker if a new spike exceeds
the current range. Shrink only after five seconds without a larger sample.

## Input contract

The host supplies all controller events before its game input handler. SDL2
maps Guide, D-pad and positional South/East (SDL A/B), not printed letters.
Raw joystick events must not be fed a second time beside mapped controller
events. The library does not grab evdev devices or intercept firmware hotkeys.

Closed-menu state machine, tracked per controller (maximum four):

1. Maintain a held-button mask even while closed. A new Guide down arms a
   candidate only if no other button or active axis/trigger is already held.
2. Any other button press while Guide is held cancels the candidate until
   Guide is released. Axis activation cancels it too. Repeated downs are
   ignored; orphan releases never create a click.
3. A matching Guide up opens only an uncancelled candidate, then consumes
   that release. No long-press timeout is needed. Chord events, including
   Guide down/up, pass through while closed; there is no delayed replay.
4. Focus loss, removal or explicit reset cancels candidates and repeats;
   it never toggles the menu. A reconnect begins unarmed until neutral.

Thus both Menu-down/Start-down and Start-down/Menu-down remain chords; a
released chord partner does not turn the remaining Guide release into a tap.
Guide in one controller never combines with another controller's buttons.
When any controller opens the menu, all controllers are captured and all
pre-existing tap candidates are cancelled. Start/Select alone never toggle it.

While open, return consumed for every controller button/axis event, including
unknown controls. Clean Guide down/up can close it; Guide plus another input
cannot. On close, drain held controls until all tracked controls are neutral,
consuming their releases so an East-up or held stick cannot leak to the game.
This drain is exposed by `spo_input_captured()`.

The host must also gate **polled** controller state while captured and clear
its already-held gameplay controls when capture starts. An event-consumed
flag alone cannot undo buttons previously forwarded to a game. The pause
callback reports menu visibility, not the drain period; it may clear game
input and decide whether to pause simulation/audio. Rendering and event
pumping continue while the game is paused. No callback changes system volume.

## Proposed C API

Names and limits below are the review contract, not a shipped header. One
process-wide instance matches the requested API. All functions are serialized
by the host on the context thread, including counter updates; worker threads
hand values to their host's existing synchronization path. The library uses
no locks, worker threads, exceptions or C++ runtime.

Use fixed-width status/ID/button types; public structs start with a byte size
for ABI validation. `SPO_ABI_VERSION` must match the library's ABI version.
No `bool`, bitfields, GL enums, SDL structs or compiler-sized enums cross FFI.
String pointers are UTF-8 input; the initial font renders printable ASCII and
replaces unsupported codepoints with `?`. Registration copies strings.

```c
#include <stdint.h>

typedef int32_t  spo_result;
typedef uint32_t spo_id;
typedef uint32_t spo_button;
typedef uint32_t spo_axis;
typedef uint32_t spo_option_type;
typedef void *(*spo_get_proc_fn)(const char *name, void *user);
typedef void (*spo_pause_fn)(uint32_t menu_open, void *user);
typedef spo_result (*spo_change_fn)(spo_id id, int32_t value, void *user);
typedef void (*spo_action_fn)(spo_id id, void *user);

typedef struct spo_config {
    uint32_t struct_size;
    uint32_t abi_version;
    spo_get_proc_fn get_proc_address;
    void *gl_user;
    const char *config_path;
    const char *port_name;
    const char *port_version;
    spo_pause_fn on_pause;
    void *pause_user;
} spo_config;

typedef struct spo_page_desc {
    uint32_t struct_size;
    const char *key;
    const char *label;
} spo_page_desc;

typedef struct spo_option_desc {
    uint32_t struct_size;
    spo_id page;
    spo_option_type type;
    const char *key;
    const char *label;
    const char *help;
    int32_t default_value;
    int32_t minimum;
    int32_t maximum;
    int32_t step;
    const char *const *choices;
    uint32_t choice_count;
    spo_change_fn on_change;
    spo_action_fn on_action;
    void *user;
} spo_option_desc;

typedef struct spo_counter_desc {
    uint32_t struct_size;
    const char *key;
    const char *label;
    const char *unit;
    uint32_t decimals;
} spo_counter_desc;

typedef struct spo_indicator_desc {
    uint32_t struct_size;
    const char *key;
    const char *label;
} spo_indicator_desc;

typedef struct spo_stats {
    uint32_t struct_size;
    uint32_t sample_frames;
    double cpu_us_mean;
    double cpu_us_max;
    double wall_us_mean;
    uint32_t draw_calls;
    uint32_t vertices;
    uint32_t telemetry_reads;
} spo_stats;

spo_result spo_init(const spo_config *config);
void spo_shutdown(void);
spo_result spo_register_page(const spo_page_desc *desc, spo_id *id);
spo_result spo_register_option(const spo_option_desc *desc, spo_id *id);
spo_result spo_register_counter(const spo_counter_desc *desc, spo_id *id);
spo_result spo_register_indicator(const spo_indicator_desc *desc, spo_id *id);
spo_result spo_option_get(spo_id id, int32_t *value);
spo_result spo_option_set(spo_id id, int32_t value);
spo_result spo_counter_set(spo_id id, double value);
spo_result spo_indicator_set(spo_id id, uint32_t on);
int32_t spo_handle_input(uint32_t controller, spo_button button,
                         uint32_t pressed);
int32_t spo_handle_axis(uint32_t controller, spo_axis axis, int32_t value);
void spo_input_reset(uint32_t controller);
int32_t spo_menu_is_open(void);
int32_t spo_input_captured(void);
spo_result spo_frame(uint32_t drawable_width, uint32_t drawable_height);
void spo_reset_timing(void);
void spo_context_lost(void);
spo_result spo_get_stats(spo_stats *stats);
int32_t spo_config_dirty(void);
spo_result spo_config_save(void);
spo_result spo_last_error(void);
const char *spo_version(void);

union SDL_Event;
int32_t spo_handle_sdl_event(const union SDL_Event *event);
```

Contract details:

- Results: OK, invalid argument/ABI, not initialized, already initialized,
  capacity, registration closed, I/O, GL unavailable, context lost and host
  change rejected. Invalid IDs return an error, never alias another object.
- Buttons: Guide, D-pad, South/East/West/North, Start/Select, shoulders,
  digital triggers, stick clicks and Other. Axes: both sticks and analog
  triggers; signed stick values -32768..32767, triggers 0..32767. A fixed 20%
  activation / 15% release hysteresis detects active axes for chord/drain
  tracking; axes never navigate the menu. Controller IDs are host-stable IDs,
  not SDL device indexes. `SPO_ALL_CONTROLLERS` resets all slots on focus loss.
- Types: Toggle 0/1; Choice zero-based index; Integer inclusive range with a
  positive step and aligned endpoints; Action with a stable key but no saved
  value. Reject malformed ranges/choices and duplicate persistence keys.
- Limits: eight port pages, eight options/page, eight counters, eight
  indicators. Page/option keys are ASCII identifiers (32 bytes maximum),
  labels 24 characters, help 80, choices 16 entries of 24 characters. Counter
  labels/units must fit a 24-character formatted row; invalid registration
  fails rather than creating an unrenderable panel. All limits exclude NUL.
- `spo_init` needs a current compatible GL context. It copies config strings,
  allocates bounded storage, compiles/uploads GL resources and loads the INI.
  A null config path means in-memory preferences only. Missing INI is normal.
  Partial initialization unwinds files and GL resources. Shutdown is safe
  after failure and repeated shutdown is a no-op.
- Register before the first `spo_frame`, which freezes registration. On option
  registration validate the saved/default value and call `on_change` once
  with that value. Callbacks return OK to accept or an error to reject without
  modifying the current value. A failed initial callback fails registration.
  Actions never execute on load. Callbacks cannot reenter the library and must
  be short; host work such as shader rebuilds is queued by the callback.
- `spo_option_set` follows the same validation/callback/persistence path as
  menu changes. Counter values are not preferences; finite values display
  with 0..3 decimal places, NaN marks unavailable, infinity is invalid.
  Indicators start unavailable; their first set supplies On/Off. These values
  are sampled into visible strings at most twice per second.
- `spo_frame` is called once immediately before each actual swap/present,
  after binding the intended color target. It does not swap, finish, read
  back or pace the game. Pass framebuffer pixels, not logical window size.
  A paused port continues calling it. Zero-size surfaces skip GL and reset
  timing. Before replacing a healthy context, shut down while it is current
  and reinit on the replacement. If it is lost, call `spo_context_lost` before
  shutdown: this makes no GL calls, abandons invalid handles and prevents
  further drawing until shutdown/reinit.
- The SDL2 helper lives in its own optional target/source. The public header
  only forward-declares its union. It maps button/axis events, device removal
  and focus loss; it neither opens controllers nor polls/changes mappings.
  While captured, it consumes raw joystick duplicates too. Non-controller
  events, including Quit, remain available to the host. Reset bookkeeping on
  lifecycle events even when returning unconsumed for the host to process.

Integration shape (full buildable examples follow approval):

```c
/* C: after init and registration, in the existing event/render loop. */
if (!spo_handle_sdl_event(&event)) port_handle_event(&event);
port_update(spo_menu_is_open());
port_draw();
spo_frame(drawable_width, drawable_height);
SDL_GL_SwapWindow(window);
```

The host's pause handler also clears game input; polled input is zeroed while
`spo_input_captured()` is true. After closing the menu, call
`spo_config_save()` if dirty at a host-selected safe point, and do the same
before shutdown. Do not retry every loop iteration after I/O failure.
`spo_shutdown` does not silently save.

```cpp
// C++: optional spo.hpp only owns init/shutdown; other calls use the C API.
spo::session overlay(config);
if (overlay.result() != SPO_OK) return overlay.result();
```

```zig
// Zig: compile/link the C library; no generated binding package.
const spo = @cImport({ @cInclude("spo.h"); });
if (spo.spo_init(&config) != spo.SPO_OK) return error.OverlayInit;
defer spo.spo_shutdown();
```

```rust
// Rust: repr(C) structs mirror spo.h; a small handwritten extern block.
unsafe extern "C" {
    fn spo_init(config: *const SpoConfig) -> i32;
    fn spo_frame(width: u32, height: u32) -> i32;
    fn spo_shutdown();
}
```

The C++ wrapper is move-only and performs no allocation or other policy.
It shuts down only if it owns a successful initialization; an Already
initialized error must not destroy another session. Destroy it before the
host destroys its GL context.
Rust examples include exact `#[repr(C)]` definitions and a build script;
Zig/Rust examples retain callbacks until shutdown and do not unwind across C.

## Timing and telemetry

`CLOCK_MONOTONIC` timestamps at entry to `spo_frame` measure **submitted
present cadence**, not scanout, GPU completion or unique game frames. VSync,
limiting and earlier overlay work affect successive intervals. No correction
subtracts the overlay's cost from reported FPS. The first call supplies no
interval. A missed call is an integration error; explicit suspend/resume uses
`spo_reset_timing`. Real stalls remain in the graph, including long ones.

Keep a fixed ring of 512 uint64 nanosecond intervals for history. Separately
accumulate interval count and duration since the last numeric refresh. At the
first frame reaching 0.5 s, publish `FPS = count / interval_seconds` and reset
the accumulator. These adjacent windows include whole intervals, so they can
exceed 0.5 s by one frame; they do not depend on history capacity. Display
frame time from the same window, `1000 / FPS`. Update numeric text at most
2 Hz, graph every presented frame.
During startup show `--` until 0.5 s is available. Do not clamp to 60/99 FPS.
If a frame alone exceeds 0.5 s it remains a real sample, not missing data.

Optional 1% low: reciprocal of the mean of the slowest `ceil(N/100)` intervals
in the last 512 intervals, minimum 120 samples. At 2 Hz, retain the six worst
values in a fixed array; no heap, full sort or per-frame percentile scan.
Label the sample window in menu help; do not call it a session-wide 1% low.

At init, discover a bounded set of sources and open read-only/CLOEXEC file
descriptors. Get page size and clock ticks once. Retain at most 24 telemetry
FDs and bounded read buffers; no repeated globbing, stat, open, subprocess or
path search in `spo_frame`. `pread(fd, buffer, size, 0)` reads each source at
most twice per second, only when an enabled item needs it. Stagger deadlines,
at most two reads per frame. Paused/menu display does not raise that rate.
Off suppresses polling and resets load baselines when re-enabled.

| Item | Candidate/source and interpretation |
| --- | --- |
| System CPU | `/proc/stat`, aggregate CPU fields; total excludes separately listed guest fields, busy excludes idle/iowait; counter deltas, 0..100% across the machine |
| Process CPU | `/proc/self/stat`, delta utime+stime / ticks / monotonic time; 100% means one occupied core and can exceed 100% |
| GPU load | Discovered GPU devfreq node plus a fixture-verified vendor adapter for load/utilization or busy/total counters; never infer load from clock |
| CPU clock | Open CPUFreq policy0 current-frequency file (or cpu0 equivalent); label CPU0, not an assertion that every core has that clock |
| GPU clock | GPU devfreq `cur_freq`; adapter specifies units, display MHz |
| Temperature | Select a named SoC/CPU thermal zone by type, retain its `temp`; explicit adapter for units, display C |
| Battery | Power-supply `type=Battery`, then `capacity` and `status`; percent plus CHG when charging |
| RSS | `/proc/self/stat` resident pages times init page size; MiB, approximate kernel accounting |
| Available memory | `/proc/meminfo` MemAvailable, KiB converted to MiB; do not substitute MemFree |

Proc field meanings and approximate RSS accounting follow the
[kernel proc documentation](https://docs.kernel.org/filesystems/proc.html).
Process names may contain spaces/parentheses; parse the parenthesized comm
field before indexing numeric fields. Reject partial/truncated/overflowing
input and reset delta baselines after a counter decrease or nonpositive time.

GPU discovery examines both devfreq names and resolved device/name metadata
for GPU/Mali identity, including sunxi and rockchip naming. Vendor formats and
units must be established by the three device fixtures; `load` is not assumed
to be a portable devfreq ABI. Discovery may also select known vendor files
under that same GPU device. Unsupported formats hide the affected item.

Each sample carries validity and monotonic age. Never show a missing value as
zero. A transient read failure removes that value until a good read; after
three consecutive failures disable that source until reinit. Values older
than 1.5 s disappear. Stop a frame's second read when the telemetry CPU slice
has already used 20 us; overdue samples are not fabricated. Benchmark delayed
reads and missing sources explicitly. A slow synchronous read is still a
budget failure; the guard cannot preempt it.

## Core, GLES backend and state isolation

Pipeline: input/settings -> cached formatted rows -> integer layout -> fixed
quad/vertex list -> backend submission. Telemetry and layout parsers have no
GL calls. The private backend interface accepts a read-only atlas at creation,
then vertices and ordered clip/draw commands at submission, then destruction.
It contains no GL handles. A future Vulkan implementation can consume this
boundary without changing input, metrics or settings. No Vulkan code now.

Use a white RGBA bitmap atlas with a solid white texel, nearest filtering and
clamp-to-edge. Text, backgrounds, graph bars and focus highlights share it.
Vertices contain float pixel XY/UV plus RGBA8 color (20 bytes); quads expand
to six triangle vertices. Capacity: 2,048 quads, allocated before frames.
Clip whole panels on CPU; usual overlay and menu each submit one draw.
Capacity exhaustion returns an error and increments a diagnostic; never
write past storage or silently discard metrics.

Budget at most 1 MiB of library CPU storage, three preallocated streaming
VBOs and a <=128x128 atlas. Never grow/orphan GL storage in a frame; upload
only the used vertex range. Driver-internal allocation cannot be promised
away by the C allocator policy: trace it where supported. Rotating VBOs may
still stall if the GPU is behind; record that cost.

Resolve required functions once, check context version and extension strings,
and fail initialization if essential entry points are absent. A non-null
function pointer alone does not prove an extension exists. Use GLES 2.0
shaders and calls in GLES 2/3 contexts and test gl4es separately. No shader
compilation, texture upload, glGetError loop, glFinish or glReadPixels per
frame. GL errors are checked during setup and in diagnostic tests.

Save actual host state on every submission. Do not assume cached state or
default bindings. Init/setup uses the same protection for any host state it
touches. Restore even on a library failure. The proposed audit list is:

| State | Backend operation and restoration |
| --- | --- |
| Program | Save/restore current program; uniforms belong only to the library program |
| Buffers | Save/restore ARRAY_BUFFER; DrawArrays avoids element-buffer mutation |
| VAO | Use a private core ES3/OES ES2 VAO where supported; save/restore binding. Never change the host VAO's element binding or descriptors |
| ES2 without VAO | For attribute slots 0..2 save/restore enable, size, type, normalized, stride, pointer and per-attribute buffer binding |
| Current vertex attributes | Save/restore generic values for slots 0..2 as well; these are outside VAO ownership |
| Textures | Save active unit; switch to unit 0, save/restore its TEXTURE_2D binding, then restore original active unit. Other units/targets are untouched |
| ES3 samplers | Save/restore unit-0 sampler binding; host sampler cannot override the font's nearest filtering |
| Blend | Save/restore enable, separate RGB/alpha equations and factors; use premultiplied output with ONE / ONE_MINUS_SRC_ALPHA. Blend constant is untouched |
| Depth | Save/restore test enable; disable test. Depth function/range/write mask remain untouched |
| Stencil | Save/restore test enable; disable test. Front/back functions, operations and masks remain untouched |
| Scissor | Save/restore enable and box |
| Cull | Save/restore enable; disable. Face mode/front winding remain untouched |
| Viewport | Save/restore four components; draw in the supplied pixel dimensions |
| Color mask | Save/restore RGBA write mask; enable all channels for overlay |
| Coverage/dither | Save/restore sample-alpha-to-coverage, sample-coverage and dither enables; disable them for predictable coverage. Coverage value/invert remain untouched |
| ES3+ raster controls | Save/restore rasterizer-discard and, where supported, sample-mask enable; disable during draw |
| Pixel store at atlas creation | Save/restore UNPACK_ALIGNMENT; on ES3 also unpack buffer binding, row length, skip rows/pixels; neutralize for CPU atlas upload. No per-frame upload |
| Untouched state | Framebuffer bindings, renderbuffers, PACK state, other pixel-store fields, texture parameters on host objects and query/transform-feedback objects remain untouched |

The host supplies a complete intended draw framebuffer. Active transform
feedback must be ended/paused and conditional rendering ended before calling;
an active host query must exclude the overlay if its results should measure
only the game. The library cannot undo query results or render into an
arbitrary host framebuffer without this cooperation. It does not bind a
default framebuffer on the host's behalf.

State ownership follows the [ES2 specification](https://registry.khronos.org/OpenGL/specs/es/2.0/es_cm_spec_2.0.pdf),
[ES3 specification](https://registry.khronos.org/OpenGL/specs/es/3.0/es_spec_3.0.pdf)
and [OES VAO specification](https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/main/extensions/OES/OES_vertex_array_object.txt).
Capability-specific states are queried only where supported. Extend the
table and state-restoration tests together whenever backend behavior changes.

## Persistence, packaging and license

The host owns the config destination; the library creates no home directory
or global settings. Fixed-capacity INI parsing happens at init. Load defaults
for malformed known values, retain valid values and report parse status.
Preserve bounded unknown sections/keys so a temporary downgrade does not
destroy later options; reject an oversized file rather than truncate it.

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
process_cpu=0
gpu_load=1
self_cost=0

[spo.counters]
draws=1

[spo.indicators]
shaders=1

[port.graphics]
vsync=1
frame_limit=1
render_scale=100
```

Each page's key scopes its option keys. Choice values are indexes; hosts must
keep choice ordering stable or migrate their config. A preset number records
the base; the complete saved item map is authoritative when custom=1.
Actions have keys for stable identity but never a persisted invocation.

Changes mark dirty in memory; `spo_frame` never writes config. The explicit
save call serializes into preallocated storage, writes a uniquely created
sibling temp file, fsyncs it, renames over the destination and fsyncs the
directory where supported. A failure leaves dirty set and preserves the last
complete config; clean up only the temp file created by this call. No periodic
auto-save retries on the render path. One process owns the file at a time.

After approval, CMake exports `spo::static`, `spo::shared` and optional
`spo::sdl2`; the adapter links exactly the host's chosen library variant and
SDL2, without pulling SDL into the base shared object. Both variants expose
only the C ABI; `spo.hpp` is optional and header-only. Offline builds use
vendored font data; no configure-time downloads. The cross-build uses the
port's aarch64 toolchain/sysroot, with an SDL2/GLES2 demo that resolves GL
through SDL and renders a repeatable steady load.

Project code uses GPL-3.0-or-later, with SPDX headers in every source/build
script. Ship GPL text as LICENSE, README with all four integration examples
and measured-cost table, CONTRIBUTING, buildable examples, tests and fixture
provenance. Ignore build output. Preserve the existing master branch and
configured commit author; no push or GitHub repository creation.

Proposed font: Spleen 5x8, release 2.1.0, reduced to the needed glyphs with a
small reproducible build-time converter. Its
[BDF metadata](https://raw.githubusercontent.com/fcambus/spleen/2.1.0/spleen-5x8.bdf)
and [BSD-2-Clause license](https://raw.githubusercontent.com/fcambus/spleen/2.1.0/LICENSE)
were checked. Before vendoring, resolve the release to an immutable commit
and record the input SHA-256, conversion command and upstream copyright.
Ship the font's original license next to the data and retain BSD attribution
on generated data. The runtime has no BDF parser or font dependency.

## Verification after design approval

Host unit tests cover observable behavior rather than renderer internals:

- All nine anchors at 640x480/1280x720; integer scaling, resize, large values,
  missing sources, maximal port registration, fit fallback and capacity error.
- Preset membership, Custom/base transitions, every item toggle and Off with
  no GL/polling; numeric refresh independent from raw graph refresh.
- INI round trips including unknown keys, missing/bad values, rejected
  callbacks, action non-replay, oversized files and simulated save failures.
- Guide alone; both chord orders; a button held before Guide; chord released
  early; repeated downs/orphan ups; Start/Select alone; axes; simultaneous
  controllers; open/close input drain; focus loss and disconnect/reconnect.
- 30/60/variable cadence, startup, genuine stalls, resume reset, FPS above
  99, ring wrap, optional 1% low reference results and stale telemetry.
- Fixture parsers: exact three-device formats plus whitespace, process comm
  parentheses, reset/wrapped counters, invalid units, truncation and overflow.

Capture proc/sysfs fixtures read-only from `knulli_40-h` (H700/KNULLI),
`amberelec-r36s` (RK3326/AmberELEC), and `spruce-flip` (RK3566/Spruce).
Store only relevant counters/metadata, replacing personal process names with
synthetic equivalents. Record device/CFW/kernel, source-relative paths, units
and capture time. Do not assume a path from another port or firmware build.

GL integration tests seed nondefault audited state and host objects, draw a
known scene, render the overlay, then compare state/object values and render
the next scene. Exercise ES2 with/without OES VAO, ES3 with a host sampler and
unpack buffer, gl4es, init failures and shutdown. Track allocator calls after
init and inspect dynamic dependencies/symbols for unwanted GL/SDL linkage.
Compile/link minimal C, C++, Zig and Rust consumers of both library variants.

On `knulli_40-h`, launch the demo through the installed firmware frontend.
Record artifact hash, firmware/kernel/GL driver, resolution, refresh, CPU
governor/affinity, achieved clocks and temperature. After warmup, interleave
Off/on/Off trials for each preset under the same steady load, including a
case without an FPS cap to expose GPU differences. Include all regular 2 Hz
poll/format frames; do not benchmark screenshots or logging in these trials.

The self item measures the complete previous `spo_frame` using thread CPU
time, plus monotonic elapsed time for detecting waits. Its visible CPU value
is a 0.5 s mean and draw count is the actual previous submission count; a
snapshot API also exposes max, vertices and telemetry reads. Timestamping
overhead is included in external harness totals. Self readings cannot prove
GPU cost or driver work on another thread, so compare whole-process CPU
between arms as well as the instrumented call costs.

Use asynchronous disjoint timer queries only in the measurement build when
available: never wait for a result and discard disjoint samples, as required
by the [timer-query specification](https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/main/extensions/EXT/EXT_disjoint_timer_query.txt).
If absent, controlled A/B reports total throughput/frame-time differences and
uncertainty; it cannot by itself isolate a <200 us GPU contribution when the
CPU or VSync is limiting. Mark that GPU limit unverified if evidence cannot
resolve it. No wall-clock subtraction presented as GPU time.

Save separate untimed screenshots of presets 0..4 and every menu page,
including representative unavailable/custom states. Record paths and hashes
in the final verification report. Acceptance needs numeric limits, state
isolation, input/hotkey behavior and cleanup; screenshots alone are not proof.

Device procedure for **every** SSH probe, fixture capture or demo run:

- Never use `muos_35-h`.
- Acquire `/tmp/pm-device-lock` atomically with mkdir and write an owner line
  identifying this task. Do not steal an existing lock. Install a trap that
  releases only this task's lock and handles partial setup failure.
- Under the lock, check that no game/unrelated test is running before doing
  work; if busy, release the lock and continue local work.
- Limit each run to under ten minutes. Leave at least two minutes between
  runs, retaining shared last-run timing so another agent's run counts too.
- Volume must already be 0. If it is not, do not run. Never raise, restore,
  mute, unmute or write audio controls, including during cleanup.
- Use existing launch/capture helpers after inspecting them. Track every
  file/process created, restore owned frontend state, and remove all deployed
  artifacts before releasing ownership. Copy evidence locally first.
  Any scripted recursive delete must quote paths, use `${var:?}`, validate
  the expected root and reject empty/root paths, including over SSH.

## Review gate and remaining work

Review the menu hierarchy, compact Full layout, input/callback contract,
explicit safe-point saves and fit-to-screen size behavior before coding.
The stricter average CPU allowance and synchronous-read/GL-query risks need
to remain visible throughout implementation.

After approval: implement core/API, persistence/input tests, metric adapters
and fixtures, GLES isolation, language examples and demo; then cross-build,
measure on H700 and capture screenshots. Pin the font before vendoring.
No cost result or screenshot exists yet. Phase 1 stops here for user review.
