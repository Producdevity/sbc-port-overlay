#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
host=${1:?SSH alias required}
ssh -o BatchMode=yes -o ConnectTimeout=8 "$host" sh -s <<'REMOTE'
set -eu
lock=/tmp/pm-device-lock
mkdir "$lock" || { echo 'BUSY: device lock exists'; exit 75; }
owner="sbc-port-overlay-inventory-$$"
ran=0
cleanup() {
    if [ "$(cat "$lock/owner" 2>/dev/null)" = "$owner" ]; then
        if [ "$ran" = 1 ]; then date +%s > /tmp/pm-device-last-run; fi
        rm -f "$lock/owner"
        rmdir "$lock"
    elif [ ! -e "$lock/owner" ]; then rmdir "$lock" 2>/dev/null || :
    fi
}
trap cleanup EXIT HUP INT TERM
printf '%s\n' "$owner" > "$lock/owner"
now=$(date +%s)
last=$(cat /tmp/pm-device-last-run 2>/dev/null || echo 0)
case "$last" in ''|*[!0-9]*) echo 'Unknown cooldown record'; exit 75;; esac
if [ "$((now-last))" -lt 120 ]; then echo 'BUSY: cooldown'; exit 75; fi
ran=1
echo 'PROCESS NAMES (review before launch)'
ps -eo pid,comm
if pgrep -f '[/]ports/.*(\.sh|/)|[/]Roms/PORTS/' >/dev/null; then echo 'BUSY: game/port process'; exit 75; fi
echo 'IDENTITY'
uname -m
uname -r
cat /etc/os-release
echo 'TOOLS'
for tool in cc gcc cmake curl amixer fbgrab ffmpeg grim timeout; do command -v "$tool" || :; done
echo 'READ-ONLY VOLUME'
if command -v amixer >/dev/null 2>&1; then amixer sget Master 2>/dev/null || amixer scontents 2>/dev/null; fi
if command -v knulli-audio >/dev/null 2>&1; then knulli-audio getSystemVolume; fi
REMOTE
