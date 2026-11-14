#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
umask 077
lock=/tmp/pm-device-lock
owner="sbc-port-overlay-run-$$"
root="/tmp/spo-verify-$$"
[ "${SPO_PREPARED:-0}" = 1 ] || exit 75
[ "$(cat "$lock/owner")" = "$owner" ] && [ "$(cat "$root/owner")" = "$owner" ] || exit 75
pid=
frontend=
frontend_start=
frontend_exe=
suspended=0
ran=0
volume_before=
clock_controls() {
    for f in /sys/devices/system/cpu/cpufreq/policy0/scaling_governor /sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq /sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq /sys/class/devfreq/gpu/governor /sys/class/devfreq/gpu/min_freq /sys/class/devfreq/gpu/max_freq; do
        if [ -r "$f" ]; then printf '%s=' "$f"; cat "$f"; fi
    done
}
same_frontend() {
    [ -n "$frontend" ] &&
    [ "$(readlink -f "/proc/$frontend/exe" 2>/dev/null)" = "$frontend_exe" ] &&
    [ "$(awk '{print $22}' "/proc/$frontend/stat" 2>/dev/null)" = "$frontend_start" ]
}
cleanup() {
    status=$?
    trap - EXIT HUP INT TERM
    set +e
    failed=0
    if [ -n "$pid" ] && [ "$(readlink -f "/proc/$pid/exe" 2>/dev/null)" = "$root/spo_demo" ]; then
        kill -TERM "$pid" 2>/dev/null
        for n in $(seq 1 15); do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
        if [ "$(readlink -f "/proc/$pid/exe" 2>/dev/null)" = "$root/spo_demo" ]; then kill -KILL "$pid" 2>/dev/null; fi
    fi
    if [ "$suspended" = 1 ]; then
        if same_frontend; then
            kill -CONT "$frontend" || failed=1
            sleep 1
            same_frontend || failed=1
            grep -q '^State:.*T' "/proc/$frontend/status" && failed=1
            case "$(curl -s -m 5 http://127.0.0.1:1234/isIdle)" in *true*) ;; *) failed=1;; esac
        else failed=1; fi
    fi
    if [ -n "$volume_before" ]; then
        [ "$(knulli-audio getSystemVolume)" = "$volume_before" ] || failed=1
    fi
    if [ -f "$root/clock-controls-before.txt" ]; then
        clock_controls > "$root/clock-controls-after.txt"
        cmp -s "$root/clock-controls-before.txt" "$root/clock-controls-after.txt" || failed=1
    fi
    case "$root" in /tmp/spo-verify-*) ;; *) exit 1;; esac
    suffix=${root#/tmp/spo-verify-}
    case "$suffix" in ''|*[!0-9]*) failed=1;; *)
        if [ "$(cat "$root/owner" 2>/dev/null)" = "$owner" ]; then rm -rf -- "${root:?}" || failed=1; else failed=1; fi;;
    esac
    if [ "$(cat "$lock/owner" 2>/dev/null)" = "$owner" ]; then
        if [ "$ran" = 1 ]; then
            stamp=$(date +%s); printf '%s\n' "$stamp" > /tmp/pm-device-last-run
            printf 'cooldown_until_epoch=%s\n' "$((stamp+120))" >&2
        fi
        rm -f "$lock/owner" || failed=1
        rmdir "$lock" || failed=1
    fi
    if [ "$failed" != 0 ]; then echo 'Cleanup verification failed; no audio or clock controls were written' >&2; exit 1; fi
    if [ "$ran" = 1 ]; then echo 'Cleanup verified: same frontend resumed, owned files removed, volume 0 and clock policies unchanged; no mixer or clock commands' >&2; fi
    exit "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
read -r preset mode duration menu uncapped maxitems work fps < "$root/params"
case "$preset" in suite|0|1|2|3|4) ;; *) exit 2;; esac
case "$mode" in benchmark|capture) ;; *) exit 2;; esac
case "$duration" in ''|*[!0-9]*) exit 2;; esac
[ "$duration" -ge 3 ] && [ "$duration" -le 45 ] || exit 2
case "$menu" in -1|0|1|2) ;; *) exit 2;; esac
case "$uncapped:$maxitems" in [01]:[01]) ;; *) exit 2;; esac
case "$work" in ''|*[!0-9]*) exit 2;; esac
[ "$work" -ge 1 ] && [ "$work" -le 24 ] || exit 2
case "$fps" in 0|60) ;; *) exit 2;; esac
case "$(curl -s -m 3 http://127.0.0.1:1234/isIdle)" in *true*) ;; *) exit 75;; esac
case "$(curl -s -m 3 http://127.0.0.1:1234/runningGame)" in *'NO GAME RUNNING'*) ;; *) exit 75;; esac
volume_before=$(knulli-audio getSystemVolume)
[ "$volume_before" = 0 ] || exit 75
frontend=$(pidof emulationstation)
case "$frontend" in ''|*[!0-9]*) exit 75;; esac
frontend_start=$(awk '{print $22}' "/proc/$frontend/stat")
frontend_exe=$(readlink -f "/proc/$frontend/exe")
clock_controls > "$root/clock-controls-before.txt"
metadata() {
    date -u '+UTC=%Y-%m-%dT%H:%M:%SZ'
    uname -m; uname -r; cat /etc/os-release
    printf 'frontend=%s\nfrontend_exe=%s\nisolated_diagnostic=frontend_SIGSTOP_SIGCONT\nvolume=%s\n' "$frontend" "$frontend_exe" "$volume_before"
    for f in /sys/firmware/devicetree/base/model /sys/firmware/devicetree/base/compatible; do tr '\000' '\n' < "$f"; done
    for f in /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq /sys/class/devfreq/gpu/cur_freq /sys/class/thermal/thermal_zone0/temp; do if [ -r "$f" ]; then printf '%s=' "$f"; cat "$f"; fi; done
}
metadata > "$root/before.txt"
sha256sum "$root/spo_demo" /usr/lib/libSDL2-2.0.so.0 >> "$root/before.txt"
mkdir "$root/home" "$root/cache" "$root/config" "$root/captures"
chmod +x "$root/spo_demo"
set -- --seconds "$duration" --work "$work" --fps "$fps"
if [ "$preset" = suite ]; then set -- "$@" --suite; else set -- "$@" --preset "$preset"; fi
if [ "$mode" = capture ]; then
    if [ "$preset" = suite ]; then set -- "$@" --capture-dir "$root/captures"; else set -- "$@" --capture "$root/captures/capture.bmp"; fi
fi
[ "$menu" = -1 ] || set -- "$@" --menu "$menu"
[ "$uncapped" = 0 ] || set -- "$@" --uncapped
[ "$maxitems" = 0 ] || set -- "$@" --max-items
suspended=1
kill -STOP "$frontend"
sleep 1
grep -q '^State:.*T' "/proc/$frontend/status" || exit 1
ran=1
(
    export HOME="$root/home" XDG_CACHE_HOME="$root/cache" XDG_CONFIG_HOME="$root/config"
    export SDL_GAMECONTROLLERCONFIG_FILE=/tmp/gamecontrollerdb.txt
    unset LD_PRELOAD
    cd "$root"
    exec ./spo_demo "$@"
) > "$root/run.log" 2>&1 &
pid=$!
for n in $(seq 1 "$((duration*9+25))"); do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
if kill -0 "$pid" 2>/dev/null; then echo 'Demo exceeded time limit' >&2; exit 1; fi
status=0
wait "$pid" || status=$?
printf '%s\n' "$status" > "$root/exit.status"
metadata > "$root/after.txt"
clock_controls > "$root/clock-controls-after.txt"
tar -C "$root" -cf - run.log before.txt after.txt params exit.status captures clock-controls-before.txt clock-controls-after.txt
[ "$status" = 0 ]
