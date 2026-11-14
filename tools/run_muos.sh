#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
preset=${1:-suite}
mode=${2:-benchmark}
duration=${3:-12}
uncapped=${4:-0}
major=${5:-2}
maxitems=${6:-0}
cpu_khz=${7:-0}
work=${8:-24}
fps=${9:-0}
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
package=$(mktemp -d "${TMPDIR:-/tmp}/spo-muos-package.XXXXXX")
cleanup_local() {
    case "$package" in */spo-muos-package.*)
        rm -f "${package:?}/spo_demo" "$package/params" "$package/muos_run_remote.sh"
        rmdir "$package";;
    esac
}
trap cleanup_local EXIT HUP INT TERM
cp build-aarch64/spo_demo "$package/spo_demo"
cp tools/muos_run_remote.sh "$package/muos_run_remote.sh"
printf '%s %s %s %s %s %s %s %s %s\n' "$preset" "$mode" "$duration" "$uncapped" "$major" "$maxitems" "$cpu_khz" "$work" "$fps" > "$package/params"
remote='set -eu
umask 077
lock=/tmp/pm-device-lock
mkdir "$lock" || exit 75
owner="sbc-port-overlay-muos-run-$$"
root="/tmp/spo-muos-$$"
created=0
cleanup() {
    status=$?
    trap - EXIT HUP INT TERM
    if [ "$created" = 1 ] && [ "$(cat "$root/owner" 2>/dev/null)" = "$owner" ]; then
        suffix=${root#/tmp/spo-muos-}
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
for other in $(pgrep -f "mux/launch.s[h]|[/]ports/.*(\\.sh|/)|[/]Roms/PORTS/" || :); do [ "$other" = "$$" ] || exit 75; done
[ -n "$(pidof muxfrontend)" ] && [ ! -s /run/muos/content ] && [ ! -s /run/muos/application ] || exit 75
[ "$(cat /opt/muos/config/settings/general/volume)" = 0 ] || exit 75
mkdir "$root"
created=1
printf "%s\n" "$owner" > "$root/owner"
tar -xf - -C "$root"
SPO_PREPARED=1 exec sh "$root/muos_run_remote.sh"'
COPYFILE_DISABLE=1 tar -C "$package" -cf - spo_demo params muos_run_remote.sh |
    ssh -o BatchMode=yes -o ConnectTimeout=8 "${SPO_SSH_HOST:?set SPO_SSH_HOST to the device SSH host}" "$remote"
