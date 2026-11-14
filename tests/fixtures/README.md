<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Device fixtures

`rk3566-spruce` contains read-only captures from the Spruce Miyoo Flip fixture
target. Its manifest records UTC capture time, kernel and original proc/sysfs
paths. Capture ran under the shared device lock, without launching an app,
stopping the frontend or changing audio. The process fixture is the capture
utility's own process; no personal process names were collected.

The GPU's load format is `0@200000000Hz`, current frequency is in Hz, and the
selected SoC thermal zone is `soc-thermal` with temperature in millidegrees C.
CPUFreq is in kHz. Battery exposes capacity and status separately.

`h700-knulli` contains the corresponding H700 capture (KNULLI 2026-05-11,
kernel 4.9.170). CPU temperature is `cpu_thermal_zone`; devfreq `gpu` exposes
clock but no readable load/utilization file in the captured candidates.
That GPU load item stays unavailable. The read-only volume probe reported 0.

RK3326/AmberELEC capture is pending SSH availability. Missing fixtures must
not be replaced by synthetic data presented as device captures.
