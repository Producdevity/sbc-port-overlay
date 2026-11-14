/* SPDX-License-Identifier: MIT */
#include "spo.h"
#include <stdio.h>
int main(void) {
    spo_config c = {0};
    spo_id counter;
    spo_counter_desc d = {sizeof(d), "draws", "DRAWS", "", 0};
    c.struct_size = sizeof(c);
    c.abi_version = SPO_ABI_VERSION;
    c.port_name = "C integration";
    c.port_version = "1.0";
    c.flags = SPO_INIT_NO_RENDER | SPO_INIT_NO_SAMPLER_THREAD;
    if (spo_init(&c) != SPO_OK)
        return 1;
    if (spo_register_counter(&d, &counter) != SPO_OK)
        return 2;
    spo_counter_set(counter, 812);
    spo_preset_set(4);
    if (spo_sample() != SPO_OK)
        return 3;
    if (spo_frame(640, 480) != SPO_OK)
        return 3;
    printf("C ABI %u: %s\n", SPO_ABI_VERSION, spo_version());
    spo_shutdown();
    return 0;
}
