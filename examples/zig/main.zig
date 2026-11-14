// SPDX-License-Identifier: GPL-3.0-or-later
const std = @import("std");
const spo = @cImport({ @cInclude("spo.h"); });
pub fn main() !void {
    var config = std.mem.zeroes(spo.spo_config);
    config.struct_size = @sizeOf(spo.spo_config);
    config.abi_version = spo.SPO_ABI_VERSION;
    config.flags = spo.SPO_INIT_NO_RENDER | spo.SPO_INIT_NO_SAMPLER_THREAD;
    config.port_name = "Zig integration";
    if (spo.spo_init(&config) != spo.SPO_OK) return error.OverlayInit;
    defer spo.spo_shutdown();
    if (spo.spo_sample() != spo.SPO_OK) return error.OverlaySample;
    if (spo.spo_frame(640, 480) != spo.SPO_OK) return error.OverlayFrame;
}
