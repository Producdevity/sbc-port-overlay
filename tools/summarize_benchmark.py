# SPDX-License-Identifier: MIT
"""Summarize an interleaved suite; GPU query deltas are estimates, not isolated timers."""
import csv
from pathlib import Path
import sys


def parse(text):
    phases = []
    current = None
    for row in csv.reader(text.splitlines()):
        if not row:
            continue
        if row[0] == "phase":
            current = {"phase": int(row[1]), "preset": int(row[3])}
            phases.append(current)
        elif current is not None and row[0] in ("cpu", "wall", "gpu_query", "gpu"):
            if row[1] != "unavailable":
                name = "gpu_query" if row[0] == "gpu" else row[0]
                current[name] = [float(value) for value in row[1:]]
        elif current is not None and row[0] in (
            "fps", "overlay_cpu_percent", "process_cpu_percent", "draw_calls",
            "sampler_threaded", "sampler_nice", "sampler_policy", "sampler_cpu_us",
            "sampler_window_s", "sampler_cycles", "sampler_reads", "sampler_core_percent",
            "library_core_percent", "sampler_cpu_us_per_frame", "warmup_cpu_mean_us",
            "warmup_cpu_max_us",
        ):
            current[row[0]] = float(row[1])
    if [phase["preset"] for phase in phases] != [0, 1, 0, 2, 0, 3, 0, 4, 0]:
        raise ValueError("expected nine complete interleaved Off/preset phases")
    for i, phase in enumerate(phases):
        if phase["phase"] != i or not all(key in phase for key in ("cpu", "fps", "overlay_cpu_percent", "process_cpu_percent", "draw_calls")):
            raise ValueError("incomplete phase")
    return phases


def rows(phases):
    for i, phase in enumerate(phases):
        result = {
            "phase": i, "preset": phase["preset"], "samples": int(phase["cpu"][0]),
            "cpu_mean_us": phase["cpu"][1], "cpu_p95_us": phase["cpu"][2],
            "cpu_p99_us": phase["cpu"][3], "cpu_max_us": phase["cpu"][4],
            "cpu_one_core_percent": phase["overlay_cpu_percent"], "fps": phase["fps"],
            "draws": int(phase["draw_calls"]), "gpu_query_mean_us": "",
            "gpu_query_delta_estimate_us": "", "fps_change_percent": "",
            "process_cpu_delta_percentage_points": "",
        }
        for name in (
            "sampler_threaded", "sampler_nice", "sampler_policy", "sampler_cpu_us",
            "sampler_window_s", "sampler_cycles", "sampler_reads", "sampler_core_percent",
            "library_core_percent", "sampler_cpu_us_per_frame", "warmup_cpu_mean_us",
            "warmup_cpu_max_us",
        ):
            result[name] = phase.get(name, "")
        if "gpu_query" in phase:
            result["gpu_query_mean_us"] = phase["gpu_query"][1]
        if phase["preset"]:
            left, right = phases[i - 1], phases[i + 1]
            baseline_fps = (left["fps"] + right["fps"]) / 2
            result["fps_change_percent"] = (phase["fps"] / baseline_fps - 1) * 100
            result["process_cpu_delta_percentage_points"] = phase["process_cpu_percent"] - (left["process_cpu_percent"] + right["process_cpu_percent"]) / 2
            if all("gpu_query" in item for item in (left, phase, right)):
                result["gpu_query_delta_estimate_us"] = phase["gpu_query"][1] - (left["gpu_query"][1] + right["gpu_query"][1]) / 2
        yield result


if __name__ == "__main__":
    output = list(rows(parse(Path(sys.argv[1]).read_text())))
    writer = csv.DictWriter(sys.stdout, fieldnames=output[0].keys())
    writer.writeheader()
    writer.writerows(output)
