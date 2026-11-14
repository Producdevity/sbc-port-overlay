#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
umask 077
lock=/tmp/pm-device-lock
owner="sbc-port-overlay-muos-run-$$"
root="/tmp/spo-muos-$$"
created=0
if [ "${SPO_PREPARED:-0}" = 1 ]; then
    [ "$(cat "$lock/owner")" = "$owner" ] && [ "$(cat "$root/owner")" = "$owner" ] || exit 75
    created=1
else
    mkdir "$lock" || { echo 'Device lock is held' >&2; exit 75; }
fi
suspended=0
ran=0
pid=
frontend=
frontend_start=
clock_changed=0
governor=
audio_before=
held_key=
held_axis=0
same_frontend() {
    [ -n "$frontend" ] &&
    [ "$(readlink -f "/proc/$frontend/exe" 2>/dev/null)" = /opt/muos/frontend/muxfrontend ] &&
    [ "$(awk '{print $22}' "/proc/$frontend/stat" 2>/dev/null)" = "$frontend_start" ]
}
cleanup() {
    original_status=$?
    trap - EXIT HUP INT TERM
    set +e
    failed=0
    if [ "$suspended" = 1 ]; then
        if [ -n "$held_key" ]; then timeout 2 evemu-event /dev/input/event3 --type EV_KEY --code "$held_key" --value 0 --sync || failed=1; fi
        if [ "$held_axis" = 1 ]; then timeout 2 evemu-event /dev/input/event3 --type EV_ABS --code ABS_HAT0Y --value 0 --sync || failed=1; fi
    fi
    if [ -n "$pid" ] && [ "$(readlink -f "/proc/$pid/exe" 2>/dev/null)" = "$root/spo_demo" ]; then
        kill -TERM "$pid" 2>/dev/null
        for n in $(seq 1 10); do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
        if [ "$(readlink -f "/proc/$pid/exe" 2>/dev/null)" = "$root/spo_demo" ]; then
            kill -KILL "$pid" 2>/dev/null
        fi
    fi
    if [ "$suspended" = 1 ]; then
        if [ "$clock_changed" = 1 ]; then
            printf '%s' "$governor" > /sys/devices/system/cpu/cpufreq/policy0/scaling_governor || failed=1
            [ "$(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_governor)" = "$governor" ] || failed=1
        fi
        if same_frontend; then
            kill -CONT "$frontend" || failed=1
            sleep 1
            same_frontend || failed=1
            grep -q '^State:.*T' "/proc/$frontend/status" && failed=1
        else
            failed=1
        fi
    fi
    if [ "$created" = 1 ]; then
        if [ -n "$audio_before" ]; then
            audio_after=$(XDG_RUNTIME_DIR=/run wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null)
            if [ "$audio_after" != "$audio_before" ] || [ "$(cat /opt/muos/config/settings/general/volume)" != 0 ]; then
                echo 'Audio state changed externally; no audio controls were written' >&2
                failed=1
            fi
        fi
        if [ -f "$root/mixer-before.txt" ]; then
            amixer -c 0 scontents > "$root/mixer-after.txt" 2>/dev/null
            cmp -s "$root/mixer-before.txt" "$root/mixer-after.txt" || failed=1
        fi
        case "$root" in /tmp/spo-muos-*)
            suffix=${root#/tmp/spo-muos-}
            case "$suffix" in ''|*[!0-9]*) failed=1;; *)
                if [ "$(cat "$root/owner" 2>/dev/null)" = "$owner" ]; then
                    rm -rf -- "${root:?}" || failed=1
                else failed=1; fi;;
            esac;;
        *) failed=1;; esac
    fi
    if [ "$(cat "$lock/owner" 2>/dev/null)" = "$owner" ]; then
        if [ "$ran" = 1 ]; then
            stamp=$(date +%s)
            printf '%s\n' "$stamp" > /tmp/pm-device-last-run
            printf 'cooldown_until_epoch=%s\n' "$((stamp+120))" >&2
        fi
        rm -f "$lock/owner" || failed=1
        rmdir "$lock" || failed=1
    fi
    if [ "$failed" != 0 ]; then
        echo 'Device cleanup/state verification failed; inspect before retrying' >&2
        exit 1
    fi
    if [ "$ran" = 1 ]; then
        echo 'Cleanup verified: demo removed, same frontend resumed, volume/mute/mixer unchanged' >&2
    fi
    exit "$original_status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
printf '%s\n' "$owner" > "$lock/owner"
last=$(cat /tmp/pm-device-last-run 2>/dev/null || echo 0)
now=$(date +%s)
case "$last" in ''|*[!0-9]*) exit 75;; esac
[ "$((now-last))" -ge 120 ] || { echo 'Device cooldown active' >&2; exit 75; }
for other in $(pgrep -f 'mux/launch.s[h]|[/]ports/.*(\.sh|/)|[/]Roms/PORTS/' || :); do
    [ "$other" = "$$" ] || { echo 'Game/port process is active' >&2; exit 75; }
done
frontend=$(pidof muxfrontend)
case "$frontend" in ''|*[!0-9]*) echo 'Expected one idle frontend' >&2; exit 75;; esac
frontend_start=$(awk '{print $22}' "/proc/$frontend/stat")
same_frontend || exit 75
[ ! -s /run/muos/content ] && [ ! -s /run/muos/application ] && [ ! -e /run/muos/safe_quit ] || exit 75
[ "$(cat /opt/muos/device/config/board/name)" = rg35xx-h ] || exit 75
[ "$(cat /opt/muos/config/settings/general/volume)" = 0 ] || exit 75
audio_before=$(XDG_RUNTIME_DIR=/run wpctl get-volume @DEFAULT_AUDIO_SINK@)
case "$audio_before" in 'Volume: 0.00'|'Volume: 0.00 [MUTED]') ;; *) echo 'Volume is not zero' >&2; exit 75;; esac
if [ "$created" = 0 ]; then
    mkdir "$root"
    created=1
    printf '%s\n' "$owner" > "$root/owner"
    tar -xf - -C "$root"
fi
amixer -c 0 scontents > "$root/mixer-before.txt"
read -r preset mode duration uncapped major maxitems cpu_khz work fps < "$root/params"
case "$preset" in suite|0|1|2|3|4) ;; *) exit 2;; esac
case "$mode" in benchmark|capture|input) ;; *) exit 2;; esac
case "$fps" in 0|60) ;; *) exit 2;; esac
[ "$mode" != input ] || [ "$preset" != suite ] || exit 2
case "$duration" in ''|*[!0-9]*) exit 2;; esac
[ "$duration" -ge 3 ] && [ "$duration" -le 45 ] || exit 2
case "$uncapped:$major:$maxitems" in [01]:[23]:[01]) ;; *) exit 2;; esac
case "$cpu_khz" in 0|1512000) ;; *) exit 2;; esac
case "$work" in ''|*[!0-9]*) exit 2;; esac
[ "$work" -ge 1 ] && [ "$work" -le 24 ] || exit 2
if [ "$cpu_khz" != 0 ]; then
    grep -qw "$cpu_khz" /sys/devices/system/cpu/cpufreq/policy0/scaling_available_frequencies || exit 2
    grep -qw userspace /sys/devices/system/cpu/cpufreq/policy0/scaling_available_governors || exit 2
    governor=$(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_governor)
    [ "$governor" != userspace ] || { echo 'Existing manual CPU clock; leave untouched' >&2; exit 75; }
fi
chmod +x "$root/spo_demo"
mkdir "$root/home" "$root/cache" "$root/config" "$root/captures"
metadata() {
    date -u '+UTC=%Y-%m-%dT%H:%M:%SZ'
    cat /etc/os-release
    uname -m; uname -r
    ldd --version 2>&1 | head -1
    printf 'board='; cat /opt/muos/device/config/board/name; echo
    printf 'model='; tr '\000' '\n' < /sys/firmware/devicetree/base/model
    printf 'compatible='; tr '\000' '\n' < /sys/firmware/devicetree/base/compatible
    printf 'audio=%s\n' "$(XDG_RUNTIME_DIR=/run wpctl get-volume @DEFAULT_AUDIO_SINK@)"
    for file in /sys/devices/system/cpu/online /sys/devices/system/cpu/cpufreq/policy0/scaling_governor /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq /sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq /sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq /sys/class/devfreq/*gpu*/cur_freq /sys/class/devfreq/*gpu*/governor /sys/class/devfreq/*gpu*/available_frequencies /sys/class/thermal/thermal_zone0/temp /sys/class/graphics/fb0/virtual_size; do
        if [ -r "$file" ]; then printf '%s=' "$file"; cat "$file"; fi
    done
}
metadata > "$root/before.txt"
{
    for tool in evemu-event evemu-describe evtest fbgrab; do command -v "$tool" || :; done
    if [ "$(cat /sys/class/input/event3/device/name)" = muOS-Keys ]; then evemu-describe /dev/input/event3; fi
    for file in /opt/muos/device/config/input/code/button/* /opt/muos/device/config/input/code/dpad/*; do
        if [ -f "$file" ]; then printf '%s=' "$file"; cat "$file"; echo; fi
    done
    for file in /sys/class/input/event*/device/name; do printf '%s=' "$file"; cat "$file"; done
} > "$root/input-capabilities.txt"
sha256sum "$root/spo_demo" /usr/lib/libSDL2-2.0.so.0 >> "$root/before.txt"
printf 'isolated_diagnostic=frontend_SIGSTOP_SIGCONT\nmode=%s preset=%s duration=%s uncapped=%s GLES=%s maxitems=%s\n' "$mode" "$preset" "$duration" "$uncapped" "$major" "$maxitems" >> "$root/before.txt"
set -- --seconds "$duration" --work "$work" --fps "$fps"
if [ "$preset" = suite ]; then set -- "$@" --suite; else set -- "$@" --preset "$preset"; fi
if [ "$mode" = capture ]; then
    if [ "$preset" = suite ]; then set -- "$@" --capture-dir "$root/captures"; else set -- "$@" --capture "$root/captures/preset-$preset.bmp"; fi
fi
[ "$uncapped" = 0 ] || set -- "$@" --uncapped
[ "$mode" != input ] || set -- "$@" --trace-input --input-node /dev/input/event3
[ "$major" = 2 ] || set -- "$@" --gles3
[ "$maxitems" = 0 ] || set -- "$@" --max-items
kill -STOP "$frontend"
suspended=1
sleep 1
grep -q '^State:.*T' "/proc/$frontend/status" || exit 1
if [ "$cpu_khz" != 0 ]; then
    printf userspace > /sys/devices/system/cpu/cpufreq/policy0/scaling_governor
    clock_changed=1
    printf '%s' "$cpu_khz" > /sys/devices/system/cpu/cpufreq/policy0/scaling_setspeed
    printf 'requested_cpu_khz=%s\n' "$cpu_khz" >> "$root/before.txt"
    printf 'attained_cpu_khz=%s\n' "$(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq)" >> "$root/before.txt"
fi
ran=1
(
    # Match the inspected SDL selection without its global symlink/cache writes.
    if [ "$(cat /opt/muos/config/settings/remap/layout)" = 1 ]; then style=modern; else style=retro; fi
    export SDL_GAMECONTROLLERCONFIG_FILE="/opt/muos/share/info/gamecontrollerdb/$style.txt"
    name=$(cat /opt/muos/device/config/sdl/name)
    SDL_GAMECONTROLLERCONFIG=$(grep -F "$name" "$SDL_GAMECONTROLLERCONFIG_FILE" || :)
    [ -n "$SDL_GAMECONTROLLERCONFIG" ] || SDL_GAMECONTROLLERCONFIG=$(cat /opt/muos/device/config/board/sdl_map)
    export SDL_GAMECONTROLLERCONFIG
    if [ "$(cat /opt/muos/config/boot/device_mode)" = 1 ]; then
        export SDL_HQ_SCALER=2 SDL_ROTATION=0 SDL_BLITTER_DISABLED=1
    else
        export SDL_HQ_SCALER="$(cat /opt/muos/device/config/sdl/scaler)"
        export SDL_ROTATION="$(cat /opt/muos/device/config/sdl/rotation)"
        export SDL_BLITTER_DISABLED="$(cat /opt/muos/device/config/sdl/blitter_disabled)"
    fi
    export SDL_ASSERT=always_ignore LD_LIBRARY_PATH=/opt/muos/frontend/lib:/usr/lib
    unset LD_PRELOAD
    export HOME="$root/home" XDG_CACHE_HOME="$root/cache" XDG_CONFIG_HOME="$root/config"
    export XDG_RUNTIME_DIR=/run
    cd "$root"
    exec ./spo_demo "$@"
) > "$root/run.log" 2>&1 &
pid=$!
printf '%s\n' "$pid" > "$root/app.pid"
if [ "$mode" = input ]; then
    [ "$(cat /sys/class/input/event3/device/name)" = muOS-Keys ] || exit 1
    ready=0
    for n in $(seq 1 15); do
        if grep -q '^input_grab,1' "$root/run.log" && ls -l "/proc/$pid/fd" | grep -q '/dev/input/event3'; then ready=1; break; fi
        sleep 1
    done
    [ "$ready" = 1 ] || { echo 'SDL input path not ready' >&2; exit 1; }
    sleep 1
    click() {
        [ "$(readlink -f "/proc/$pid/exe" 2>/dev/null)" = "$root/spo_demo" ] || exit 1
        printf '%s\n' "$1" >> "$root/input-plan.txt"
        held_key=$1
        timeout 2 evemu-event /dev/input/event3 --type EV_KEY --code "$1" --value 1 --sync
        sleep 0.12
        timeout 2 evemu-event /dev/input/event3 --type EV_KEY --code "$1" --value 0 --sync
        held_key=
        sleep 0.18
    }
    click BTN_START
    click BTN_SELECT
    click BTN_MODE
    grep -q '^input,guide,0,consumed,1,menu_before,0,menu_after,1' "$root/run.log" || exit 1
    held_axis=1
    timeout 2 evemu-event /dev/input/event3 --type EV_ABS --code ABS_HAT0Y --value 1 --sync
    sleep 0.12
    timeout 2 evemu-event /dev/input/event3 --type EV_ABS --code ABS_HAT0Y --value 0 --sync
    held_axis=0
    sleep 0.18
    click BTN_EAST
    click BTN_SOUTH
    click BTN_SOUTH
    grep -q '^input,b,1,consumed,1,menu_before,1,menu_after,0' "$root/run.log" || exit 1
fi
limit=$((duration*9+25))
for n in $(seq 1 "$limit"); do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
if kill -0 "$pid" 2>/dev/null; then echo 'Demo exceeded its time limit' >&2; exit 1; fi
status=0
wait "$pid" || status=$?
printf '%s\n' "$status" > "$root/exit.status"
metadata > "$root/after.txt"
if [ -f "$root/input-plan.txt" ]; then cp "$root/input-plan.txt" "$root/captures/input-plan.txt"; fi
tar -C "$root" -cf - run.log before.txt after.txt params exit.status captures mixer-before.txt input-capabilities.txt
[ "$status" = 0 ]
