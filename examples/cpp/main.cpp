// SPDX-License-Identifier: MIT
#include "spo.hpp"
int main() {
    spo_config config{};
    config.struct_size = sizeof(config);
    config.abi_version = SPO_ABI_VERSION;
    config.flags = SPO_INIT_NO_RENDER | SPO_INIT_NO_SAMPLER_THREAD;
    config.port_name = "C++ integration";
    spo::session overlay(config);
    if (overlay.result() != SPO_OK)
        return 1;
    spo::session moved(std::move(overlay));
    if (spo_sample() != SPO_OK)
        return 2;
    return spo_frame(640, 480) == SPO_OK ? 0 : 2;
}
