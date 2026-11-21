/* SPDX-License-Identifier: MIT */
#ifndef SPO_SETTINGS_H
#define SPO_SETTINGS_H
#include "spo.h"
/* Registers the pages and options a settings file declares. Call after spo_init and before the
   first spo_frame. Lines that cannot be used are reported on stderr and skipped. Changes made in
   the menu afterwards show a notice that they apply at the next start. */
spo_result spo_settings_load(const char *path);
spo_result spo_settings_parse(char *text); /* modifies text */
#endif
