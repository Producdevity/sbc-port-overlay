#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
host=${1:?SSH alias required}
ssh -o BatchMode=yes -o ConnectTimeout=8 "$host" sh -s <<'REMOTE'
set -eu
lock=/tmp/pm-device-lock
mkdir "$lock" || exit 75
owner="sbc-port-overlay-fixtures-$$"
tmp="/tmp/spo-fixtures-$$"
ran=0
cleanup() {
    case "$tmp" in /tmp/spo-fixtures-[0-9]*)
        if [ -d "$tmp" ]; then rm -f "${tmp:?}"/*; rmdir "$tmp"; fi;;
    esac
    if [ "$(cat "$lock/owner" 2>/dev/null)" = "$owner" ]; then
        if [ "$ran" = 1 ]; then date +%s > /tmp/pm-device-last-run; fi
        rm -f "$lock/owner"; rmdir "$lock"
    elif [ ! -e "$lock/owner" ]; then rmdir "$lock" 2>/dev/null || :; fi
}
trap cleanup EXIT HUP INT TERM
printf '%s\n' "$owner" > "$lock/owner"
now=$(date +%s)
last=$(cat /tmp/pm-device-last-run 2>/dev/null || echo 0)
case "$last" in ''|*[!0-9]*) exit 75;; esac
[ "$((now-last))" -ge 120 ] || exit 75
if pgrep -f '[/]ports/.*(\.sh|/)|[/]Roms/PORTS/' >/dev/null; then echo 'Game/port process active' >&2; exit 75; fi
ran=1
mkdir "$tmp"
{
    date -u '+captured_utc=%Y-%m-%dT%H:%M:%SZ'
    printf 'previous_run=%s\n' "$last"
    uname -m; uname -r
    if [ -r /etc/os-release ]; then sed -n '/^NAME=/p;/^VERSION=/p;/^OS_NAME=/p;/^OS_DATE=/p' /etc/os-release; fi
} > "$tmp/manifest.txt"
for path in /proc/stat /proc/self/stat /proc/meminfo \
    /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq \
    /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq \
    /sys/class/devfreq/*/name /sys/class/devfreq/*/cur_freq \
    /sys/class/devfreq/*/load /sys/class/devfreq/*/device/utilization \
    /sys/class/thermal/thermal_zone*/type /sys/class/thermal/thermal_zone*/temp \
    /sys/class/power_supply/*/type /sys/class/power_supply/*/capacity /sys/class/power_supply/*/status; do
    [ -r "$path" ] || continue
    name=$(printf '%s' "$path" | tr '/' '_')
    head -c 16384 "$path" > "$tmp/$name"
    printf '%s\t%s\n' "$name" "$path" >> "$tmp/manifest.txt"
done
if command -v knulli-audio >/dev/null 2>&1; then
    knulli-audio getSystemVolume > "$tmp/volume.txt" 2>&1 || :
fi
if [ -r /usr/lib/libSDL2-2.0.so.0 ]; then cp -L /usr/lib/libSDL2-2.0.so.0 "$tmp/libSDL2-2.0.so.0"; fi
tar -C "$tmp" -cf - .
REMOTE
