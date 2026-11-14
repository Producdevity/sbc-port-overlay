<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Device fixtures

Real `/proc` and `/sys` files copied from handhelds, used by the metric
parser tests. Each folder's manifest lists the original paths, the kernel and
the capture time. `tools/capture_fixtures.sh HOST` makes a new set; it only
reads files.

**`rk3566-spruce`** (Miyoo Flip, Spruce):
- GPU load reads like `0@200000000Hz`, and the GPU clock is in Hz.
- SoC temperature is in thermal zone `soc-thermal`, in millidegrees C.
- CPU frequency is in kHz.
- Battery capacity and status are separate files.

**`h700-knulli`** (H700, KNULLI 2026-05-11, kernel 4.9.170):
- CPU temperature is `cpu_thermal_zone`.
- The `gpu` devfreq node reports a clock but no load, so the GPU load item is
  hidden on this device.
