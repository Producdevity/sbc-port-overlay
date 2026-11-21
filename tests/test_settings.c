/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "settings.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *declaration =
    "# comment\n"
    "[page graphics]\n"
    "label = Graphics\n"
    "\n"
    "[option graphics.vsync]\n"
    "type = toggle\n"
    "label = Vsync\n"
    "default = 1\n"
    "\n"
    "[option graphics.msaa]\n"
    "type = choice\n"
    "label = Anti-aliasing\n"
    "help = Samples per pixel.\n"
    "choices = Off, 2x, 4x\n"
    "default = 2\n"
    "\n"
    "[page audio]\n"
    "[option audio.volume]\n"
    "type = integer\n"
    "min = 0\n"
    "max = 100\n"
    "step = 10\n"
    "default = 50\n";

/* Option ids follow registration order: vsync 1, msaa 2, volume 3. */
static void expect(spo_id id, int32_t value) {
    int32_t v = -1;
    assert(spo_option_get(id, &v) == SPO_OK && v == value);
}
static void init(const char *path) {
    spo_config c = {0};
    c.struct_size = sizeof c;
    c.abi_version = SPO_ABI_VERSION;
    c.config_path = path;
    c.flags = SPO_INIT_NO_RENDER | SPO_INIT_NO_SAMPLER_THREAD;
    assert(spo_init(&c) == SPO_OK);
}
int main(void) {
    char text[1024], path[] = "/tmp/spo-settings-XXXXXX";
    int fd = mkstemp(path);
    FILE *f;
    char bad[] = "[option nowhere.x]\ntype=toggle\n[option a]\n[page p]\nlabel\n[option p.k]\ntype=text\n";
    assert(fd >= 0);
    close(fd);
    f = fopen(path, "w");
    fputs("[spo]\nformat=1\n[port.graphics]\nmsaa=1\nvsync=0\n", f);
    fclose(f);
    init(path);
    strcpy(text, declaration);
    assert(spo_settings_parse(text) == SPO_OK);
    expect(1, 0);
    expect(2, 1);
    expect(3, 50);
    spo_shutdown();

    init(NULL);
    strcpy(text, declaration);
    assert(spo_settings_parse(text) == SPO_OK);
    expect(1, 1);
    expect(2, 2);
    assert(!spo->notice[0]);
    assert(spo_option_set(2, 1) == SPO_OK && !strcmp(spo->notice, "Applies at the next start"));
    spo_shutdown();

    init(NULL);
    strcpy(text, bad);
    assert(spo_settings_parse(text) == SPO_INVALID);
    spo_shutdown();
    assert(spo_settings_load("/nonexistent/settings.ini") == SPO_IO);
    unlink(path);
    return 0;
}
