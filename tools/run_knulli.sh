#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
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
[ "$duration" -ge 3 ] && [ "$duration" -le 120 ] || exit 2
case "$menu" in -1|0|1|2) ;; *) exit 2;; esac
case "$uncapped:$maxitems" in 0:0|0:1|1:0|1:1) ;; *) exit 2;; esac
case "$work" in ''|*[!0-9]*) exit 2;; esac
[ "$work" -ge 1 ] && [ "$work" -le 24 ] || exit 2
case "$fps" in 0|60) ;; *) exit 2;; esac
[ "$preset" != suite ] || [ "$duration" -le 45 ] || exit 2
package=$(mktemp -d "${TMPDIR:-/tmp}/spo-package.XXXXXX")
cleanup_local() {
    case "$package" in */spo-package.*) rm -f "${package:?}/spo_demo" "$package/remove_gamelist_entry.py" "$package/params"; rmdir "$package";; esac
}
trap cleanup_local EXIT HUP INT TERM
cp build-aarch64/spo_demo "$package/spo_demo"
cp tools/remove_gamelist_entry.py "$package/remove_gamelist_entry.py"
printf '%s %s %s %s %s %s %s %s\n' "$preset" "$mode" "$duration" "$menu" "$uncapped" "$maxitems" "$work" "$fps" > "$package/params"
remote=$(cat tools/knulli_run_remote.sh)
COPYFILE_DISABLE=1 tar -C "$package" -cf - spo_demo remove_gamelist_entry.py params |
    ssh -o BatchMode=yes -o ConnectTimeout=8 knulli_40-h "$remote"
