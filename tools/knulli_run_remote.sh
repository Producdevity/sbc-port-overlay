#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
umask 077
lock=/tmp/pm-device-lock
mkdir "$lock" || exit 75
owner="sbc-port-overlay-run-$$"
root="/tmp/spo-verify-$$"
ports=/userdata/roms/ports
launcher="$ports/SPO Verify-$$.sh"
created=0
launched=0
ran=0
cleanup() {
    original_status=$?
    trap - EXIT HUP INT TERM
    set +e
    cleanup_failed=0
    if [ "$created" = 1 ]; then
        if [ -f "$root/app.pid" ]; then
            pid=$(cat "$root/app.pid")
            case "$pid" in ''|*[!0-9]*) ;; *)
                if [ "$(readlink -f "/proc/$pid/exe" 2>/dev/null)" = "$root/spo_demo" ]; then
                    kill -TERM "$pid" 2>/dev/null || :
                    for n in $(seq 1 15); do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
                    if [ "$(readlink -f "/proc/$pid/exe" 2>/dev/null)" = "$root/spo_demo" ]; then kill -KILL "$pid" 2>/dev/null || :; fi
                fi;;
            esac
        fi
        if [ "$launched" = 1 ]; then
            rm -f "$launcher" || cleanup_failed=1
            curl -s -m 5 http://127.0.0.1:1234/reloadgames >/dev/null || cleanup_failed=1
            sleep 2
            for list in "$ports/gamelist.xml" /userdata/system/configs/emulationstation/gamelists/ports/gamelist.xml; do
                if [ -f "$list" ]; then
                    if grep -Fxq "$list" "$root/absent-lists"; then
                        python3 "$root/remove_gamelist_entry.py" "$list" "$launcher" --remove-empty || cleanup_failed=1
                    else python3 "$root/remove_gamelist_entry.py" "$list" "$launcher" || cleanup_failed=1; fi
                fi
            done
            curl -s -m 5 http://127.0.0.1:1234/reloadgames >/dev/null || cleanup_failed=1
            idle=0
            for n in $(seq 1 15); do
                case "$(curl -s -m 3 http://127.0.0.1:1234/isIdle)" in *true*) idle=1; break;; esac
                sleep 1
            done
            [ "$idle" = 1 ] || cleanup_failed=1
        fi
        [ "$(knulli-audio getSystemVolume)" = 0 ] || cleanup_failed=1
        amixer -c 0 scontents > "$root/mixer-after.txt"
        cmp -s "$root/mixer-before.txt" "$root/mixer-after.txt" || cleanup_failed=1
        case "$root" in /tmp/spo-verify-*)
            suffix=${root#/tmp/spo-verify-}
            case "$suffix" in ''|*[!0-9]*) cleanup_failed=1;; *)
                if [ "$(cat "$root/owner")" = "$owner" ]; then rm -rf -- "${root:?}" || cleanup_failed=1; else cleanup_failed=1; fi;;
            esac;;
        *) cleanup_failed=1;; esac
    fi
    if [ "$(cat "$lock/owner" 2>/dev/null)" = "$owner" ]; then
        if [ "$ran" = 1 ]; then
            stamp=$(date +%s); printf '%s\n' "$stamp" > /tmp/pm-device-last-run
            printf 'cooldown_until_epoch=%s\n' "$((stamp+120))" >&2
        fi
        rm -f "$lock/owner" || cleanup_failed=1; rmdir "$lock" || cleanup_failed=1
    elif [ ! -e "$lock/owner" ]; then rmdir "$lock" 2>/dev/null || :; fi
    if [ "$cleanup_failed" != 0 ]; then echo 'Device cleanup/restoration failed; inspect this run before retrying' >&2; exit 1; fi
    if [ "$ran" = 1 ]; then echo 'Cleanup verified: frontend idle, test files/entry removed, volume and mixer unchanged' >&2; fi
    exit "$original_status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
printf '%s\n' "$owner" > "$lock/owner"
last=$(cat /tmp/pm-device-last-run 2>/dev/null || echo 0); now=$(date +%s)
case "$last" in ''|*[!0-9]*) exit 75;; esac
[ "$((now-last))" -ge 120 ] || exit 75
for pid in $(pgrep -f '[/]ports/.*(\.sh|/)' || :); do
    [ "$pid" = "$$" ] || { echo 'Game/port process active' >&2; exit 75; }
done
case "$(curl -s -m 3 http://127.0.0.1:1234/isIdle)" in *true*) ;; *) exit 75;; esac
case "$(curl -s -m 3 http://127.0.0.1:1234/runningGame)" in *'NO GAME RUNNING'*) ;; *) exit 75;; esac
[ "$(knulli-audio getSystemVolume)" = 0 ] || { echo 'Volume is not 0; no changes made' >&2; exit 75; }
[ ! -e "$launcher" ] || exit 75
mkdir "$root"
created=1
printf '%s\n' "$owner" > "$root/owner"
tar -xf - -C "$root"
amixer -c 0 scontents > "$root/mixer-before.txt"
mkdir "$root/home" "$root/cache" "$root/config" "$root/captures"
: > "$root/absent-lists"
for list in "$ports/gamelist.xml" /userdata/system/configs/emulationstation/gamelists/ports/gamelist.xml; do
    [ -e "$list" ] || printf '%s\n' "$list" >> "$root/absent-lists"
done
read -r preset mode duration menu uncapped maxitems work fps < "$root/params"
case "$preset:$mode:$menu:$uncapped:$maxitems" in *[!0-9a-z:-]*) exit 2;; esac
case "$duration" in ''|*[!0-9]*) exit 2;; esac
[ "$duration" -ge 3 ] && [ "$duration" -le 120 ] || exit 2
case "$preset" in suite|0|1|2|3|4) ;; *) exit 2;; esac
case "$work" in ''|*[!0-9]*) exit 2;; esac
[ "$work" -ge 1 ] && [ "$work" -le 24 ] || exit 2
case "$fps" in 0|60) ;; *) exit 2;; esac
[ "$preset" != suite ] || [ "$duration" -le 45 ] || exit 2
chmod +x "$root/spo_demo"
{
    date -u '+UTC=%Y-%m-%dT%H:%M:%SZ'; uname -m; uname -r
    cat /etc/os-release
    for f in /sys/firmware/devicetree/base/model /sys/firmware/devicetree/base/compatible; do tr '\000' '\n' < "$f"; done
    sha256sum "$root/spo_demo"
    printf 'mode=%s preset=%s menu=%s uncapped=%s maxitems=%s\n' "$mode" "$preset" "$menu" "$uncapped" "$maxitems"
    for f in /sys/devices/system/cpu/online /sys/devices/system/cpu/cpufreq/policy0/scaling_governor /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq /sys/class/devfreq/gpu/cur_freq /sys/class/thermal/thermal_zone0/temp; do
        if [ -r "$f" ]; then printf '%s=' "$f"; cat "$f"; fi
    done
} > "$root/metadata.txt"
cat > "$launcher" <<LAUNCHER
#!/bin/sh
cd '$root' || exit 1
export HOME='$root/home' XDG_CACHE_HOME='$root/cache' XDG_CONFIG_HOME='$root/config'
set -- --seconds '$duration' --work '$work' --fps '$fps'
if [ '$preset' = suite ]; then set -- "\$@" --suite; else set -- "\$@" --preset '$preset'; fi
if [ '$mode' = capture ]; then
    if [ '$preset' = suite ]; then set -- "\$@" --capture-dir '$root/captures'; else set -- "\$@" --capture '$root/captures/capture.bmp'; fi
fi
if [ '$menu' != -1 ]; then set -- "\$@" --menu '$menu'; fi
if [ '$uncapped' = 1 ]; then set -- "\$@" --uncapped; fi
if [ '$maxitems' = 1 ]; then set -- "\$@" --max-items; fi
./spo_demo "\$@" > run.log 2>&1 &
pid=\$!
printf '%s\n' "\$pid" > app.pid
wait "\$pid"
status=\$?
printf '%s\n' "\$status" > exit.status
exit "\$status"
LAUNCHER
launched=1
ran=1
curl -s -m 5 http://127.0.0.1:1234/reloadgames >/dev/null
sleep 2
curl -s -m 5 --data-binary "$launcher" http://127.0.0.1:1234/launch >/dev/null
if [ "$preset" = suite ]; then limit=$((duration*9+30)); else limit=$((duration+30)); fi
for n in $(seq 1 "$limit"); do [ ! -f "$root/exit.status" ] || break; sleep 1; done
[ -f "$root/exit.status" ] || { echo 'Demo did not finish' >&2; exit 1; }
status=$(cat "$root/exit.status")
printf 'exit=%s\n' "$status" >> "$root/metadata.txt"
for f in /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq /sys/class/devfreq/gpu/cur_freq /sys/class/thermal/thermal_zone0/temp; do
    if [ -r "$f" ]; then printf 'after %s=' "$f" >> "$root/metadata.txt"; cat "$f" >> "$root/metadata.txt"; fi
done
tar -C "$root" -cf - run.log metadata.txt exit.status captures mixer-before.txt
[ "$status" = 0 ]
