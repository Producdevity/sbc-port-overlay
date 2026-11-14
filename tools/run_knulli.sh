#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
preset=${1:?preset 0..4 or suite required}
mode=${2:-benchmark}
duration=${3:-30}
menu=${4:--1}
uncapped=${5:-0}
maxitems=${6:-0}
work=${7:-24}
fps=${8:-0}
case "$preset" in suite|0|1|2|3|4) ;; *) exit 2;; esac
case "$mode" in benchmark|capture) ;; *) exit 2;; esac
case "$duration" in ''|*[!0-9]*) exit 2;; esac
[ "$duration" -ge 3 ] && [ "$duration" -le 45 ] || exit 2
case "$menu" in -1|0|1|2|3) ;; *) exit 2;; esac
case "$uncapped:$maxitems" in 0:0|0:1|1:0|1:1) ;; *) exit 2;; esac
case "$work" in ''|*[!0-9]*) exit 2;; esac
[ "$work" -ge 1 ] && [ "$work" -le 24 ] || exit 2
case "$fps" in 0|60) ;; *) exit 2;; esac
[ "$preset" != suite ] || [ "$duration" -le 45 ] || exit 2
package=$(mktemp -d "${TMPDIR:-/tmp}/spo-package.XXXXXX")
cleanup_local() {
    case "$package" in */spo-package.*) rm -f "${package:?}/spo_demo" "$package/knulli_run_remote.sh" "$package/params" "$package/args" "$package/backdrop.bmp"; rmdir "$package";; esac
}
trap cleanup_local EXIT HUP INT TERM
cp "${SPO_DEMO_BINARY:-build-aarch64/spo_demo}" "$package/spo_demo"
cp tools/knulli_run_remote.sh "$package/knulli_run_remote.sh"
printf '%s %s %s %s %s %s %s %s\n' "$preset" "$mode" "$duration" "$menu" "$uncapped" "$maxitems" "$work" "$fps" > "$package/params"
# SPO_DEMO_ARGS adds demo options; SPO_BACKDROP is a 24-bit BMP drawn behind the overlay.
extra=${SPO_DEMO_ARGS:-}
case "$extra" in *[!A-Za-z0-9_.\ =-]*) exit 2;; esac
printf '%s\n' "$extra" > "$package/args"
files="spo_demo knulli_run_remote.sh params args"
if [ -n "${SPO_BACKDROP:-}" ]; then cp "$SPO_BACKDROP" "$package/backdrop.bmp"; files="$files backdrop.bmp"; fi
remote='set -eu
umask 077
lock=/tmp/pm-device-lock
mkdir "$lock" || exit 75
owner="sbc-port-overlay-run-$$"
root="/tmp/spo-verify-$$"
created=0
cleanup() {
    status=$?
    trap - EXIT HUP INT TERM
    if [ "$created" = 1 ] && [ "$(cat "$root/owner" 2>/dev/null)" = "$owner" ]; then
        case "$root" in /tmp/spo-verify-*) ;; *) exit 1;; esac
        suffix=${root#/tmp/spo-verify-}
        case "$suffix" in ""|*[!0-9]*) exit 1;; *) rm -rf -- "${root:?}";; esac
    fi
    if [ "$(cat "$lock/owner" 2>/dev/null)" = "$owner" ]; then rm -f "$lock/owner"; rmdir "$lock"; fi
    exit "$status"
}
trap cleanup EXIT HUP INT TERM
printf "%s\n" "$owner" > "$lock/owner"
last=$(cat /tmp/pm-device-last-run 2>/dev/null || echo 0)
case "$last" in ""|*[!0-9]*) exit 75;; esac
[ "$(($(date +%s)-last))" -ge 120 ] || exit 75
case "$(curl -s -m 3 http://127.0.0.1:1234/isIdle)" in *true*) ;; *) exit 75;; esac
case "$(curl -s -m 3 http://127.0.0.1:1234/runningGame)" in *"NO GAME RUNNING"*) ;; *) exit 75;; esac
[ "$(knulli-audio getSystemVolume)" = 0 ] || exit 75
mkdir "$root"
created=1
printf "%s\n" "$owner" > "$root/owner"
tar -xf - -C "$root"
SPO_PREPARED=1 exec sh "$root/knulli_run_remote.sh"'
COPYFILE_DISABLE=1 tar -C "$package" -cf - $files |
    ssh -o BatchMode=yes -o ConnectTimeout=8 "${SPO_SSH_HOST:?set SPO_SSH_HOST to the device SSH host}" "$remote"
