#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
ssh -o BatchMode=yes -o ConnectTimeout=8 knulli_40-h sh -s <<'REMOTE'
set -eu
lock=/tmp/pm-device-lock
mkdir "$lock" || exit 75
owner="sbc-port-overlay-platform-$$"
ran=0
cleanup() {
    if [ "$(cat "$lock/owner" 2>/dev/null)" = "$owner" ]; then
        if [ "$ran" = 1 ]; then date +%s > /tmp/pm-device-last-run; fi
        rm -f "$lock/owner"; rmdir "$lock"
    elif [ ! -e "$lock/owner" ]; then rmdir "$lock" 2>/dev/null || :; fi
}
trap cleanup EXIT HUP INT TERM
printf '%s\n' "$owner" > "$lock/owner"
now=$(date +%s); last=$(cat /tmp/pm-device-last-run 2>/dev/null || echo 0)
case "$last" in ''|*[!0-9]*) exit 75;; esac
[ "$((now-last))" -ge 120 ] || exit 75
if pgrep -f '[/]ports/.*(\.sh|/)' >/dev/null; then exit 75; fi
ran=1
echo 'FRONTEND'
curl -s -m 3 http://127.0.0.1:1234/isIdle
curl -s -m 3 http://127.0.0.1:1234/runningGame
echo 'VOLUME'
knulli-audio getSystemVolume
echo 'GPU FILES'
gpu=$(readlink -f /sys/class/devfreq/gpu/device)
case "$gpu" in /sys/devices/*) find "$gpu" -maxdepth 2 -type f | head -100;; esac
echo 'PORTMASTER'
for dir in /userdata/system/.local/share/PortMaster /userdata/roms/ports/PortMaster; do
    if [ -f "$dir/control.txt" ]; then
        printf '%s\n' "$dir"
        sed -n '1,120p' "$dir/control.txt"
        if [ -f "$dir/mod_KNULLI.txt" ]; then sed -n '1,180p' "$dir/mod_KNULLI.txt"; fi
    fi
done
echo 'LAUNCHER EXAMPLE'
for path in /userdata/roms/ports/*.sh; do
    if grep -q 'pm_platform_helper' "$path"; then printf '%s\n' "$path"; sed -n '1,130p' "$path"; break; fi
done
echo 'RUNTIME'
ldd --version 2>&1 | head -1 || :
REMOTE
