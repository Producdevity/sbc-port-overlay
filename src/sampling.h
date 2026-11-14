/* SPDX-License-Identifier: MIT */
#ifndef SPO_SAMPLING_H
#define SPO_SAMPLING_H
#include "internal.h"
#include <pthread.h>
#include <stdatomic.h>
enum {
    SPO_PUB_VALID = SPO_ITEM_COUNT * 4,
    SPO_PUB_AVAILABLE,
    SPO_PUB_CHARGING,
    SPO_PUB_CPU,
    SPO_PUB_ELAPSED = SPO_PUB_CPU + 2,
    SPO_PUB_CYCLES = SPO_PUB_ELAPSED + 2,
    SPO_PUB_READS = SPO_PUB_CYCLES + 2,
    SPO_PUB_MAX = SPO_PUB_READS + 2,
    SPO_PUB_WORDS = SPO_PUB_MAX + 2
};
struct spo_sampler {
    spo_source sources[SPO_FDS];
    uint32_t source_count, available, seen_generation, last_mask;
    spo_metric live[SPO_ITEM_COUNT];
    uint64_t cpu_previous[2], process_previous, process_stamp;
    int cpu_baseline, process_baseline, charging;
    long ticks, page_size;
    char read_buffer[16384];
    atomic_uint enabled, generation, stopping, sampling, sequence;
    atomic_uint words[SPO_PUB_WORDS];
    uint64_t started_ns, next_cycle, cpu_total, cycles, reads, cycle_max;
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    int mutex_ready, condition_ready, thread_started, ready;
    int nice_value, policy;
};
void spo_readers_init(spo_sampler *);
void spo_readers_close(spo_sampler *);
void spo_readers_poll(spo_sampler *, uint64_t, uint32_t);
#endif
