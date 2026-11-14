/* SPDX-License-Identifier: MIT */
#ifndef SPO_MALI_H
#define SPO_MALI_H
#include <stdint.h>
typedef struct spo_mali spo_mali;
/* The kbase device node; tests point it elsewhere. */
extern const char *spo_mali_device;
/* Opens `device` as a kbase context and starts a hardware counter reader for GPU_ACTIVE.
 * Returns NULL on any failure; nothing stays open. */
spo_mali *spo_mali_open(const char *device);
/* Dumps the counters. On success `active` holds the GPU_ACTIVE cycles since the previous dump
 * and `stamp_ns` the kernel timestamp of this dump. */
int spo_mali_dump(spo_mali *, uint64_t *active, uint64_t *stamp_ns);
void spo_mali_close(spo_mali *);
/* GPU load in percent from active cycles over `elapsed_ns` at `clock_mhz`, clamped to 0..100.
 * Returns 0 when an input is unusable. */
int spo_gpu_load_percent(uint64_t active, double clock_mhz, uint64_t elapsed_ns, double *out);
/* Reads GPU_ACTIVE from the job manager block of a dump buffer. */
uint32_t spo_mali_jm_active(const uint8_t *block);
/* True for GPUs whose dumps put the job manager block first (Bifrost and later job manager GPUs).
 */
int spo_mali_gpu_supported(uint32_t gpu_id);
#endif
