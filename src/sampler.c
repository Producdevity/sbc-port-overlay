/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "sampling.h"
#include <errno.h>
#include <limits.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

_Static_assert(sizeof(unsigned) == 4 && ATOMIC_INT_LOCK_FREE == 2,
               "lock-free 32-bit atomics required");

static void pack(uint32_t *words, unsigned at, uint64_t value) {
    words[at] = (uint32_t)value;
    words[at + 1] = (uint32_t)(value >> 32);
}
static uint64_t unpack(const uint32_t *words, unsigned at) {
    return words[at] | ((uint64_t)words[at + 1] << 32);
}
static void publish(spo_sampler *sample, uint64_t call_start, int account) {
    uint32_t words[SPO_PUB_WORDS] = {0}, i;
    uint64_t now = spo_now(), bits;
    for (i = 0; i < SPO_ITEM_COUNT; i++) {
        memcpy(&bits, &sample->live[i].value, sizeof(bits));
        pack(words, i * 4, bits);
        pack(words, i * 4 + 2, sample->live[i].stamp);
        if (sample->live[i].valid)
            words[SPO_PUB_VALID] |= SPO_BIT(i);
    }
    words[SPO_PUB_AVAILABLE] = sample->available;
    words[SPO_PUB_CHARGING] = (uint32_t)sample->charging;
    pack(words, SPO_PUB_ELAPSED, now - sample->started_ns);
    pack(words, SPO_PUB_CYCLES, sample->cycles);
    pack(words, SPO_PUB_READS, sample->reads);
    pack(words, SPO_PUB_MAX, sample->cycle_max);
    /* Atomic payload words plus a sequentially consistent sequence avoid C data races. */
    atomic_fetch_add(&sample->sequence, 1);
    for (i = 0; i < SPO_PUB_WORDS; i++)
        atomic_store(&sample->words[i], words[i]);
    if (account) {
        uint64_t end = spo_cpu_now();
        if (sample->thread_started)
            sample->cpu_total = end;
        else
            sample->cpu_total += end - call_start;
    }
    atomic_store(&sample->words[SPO_PUB_CPU], (uint32_t)sample->cpu_total);
    atomic_store(&sample->words[SPO_PUB_CPU + 1], (uint32_t)(sample->cpu_total >> 32));
    atomic_fetch_add(&sample->sequence, 1);
}
static void sample_once(spo_sampler *sample, uint64_t start) {
    uint64_t now = spo_now();
    if (now >= sample->next_cycle) {
        spo_readers_poll(sample, now, atomic_load(&sample->enabled));
        sample->cycles++;
        sample->next_cycle = spo_now() + UINT64_C(1000000000);
    }
    uint64_t cost = spo_cpu_now() - start;
    if (cost > sample->cycle_max)
        sample->cycle_max = cost;
    publish(sample, start, 1);
}
#if defined(__linux__)
static void *run(void *user) {
    spo_sampler *sample = user;
    struct sched_param parameter;
    int result = setpriority(PRIO_PROCESS, 0, 19);
    if (pthread_getschedparam(pthread_self(), &sample->policy, &parameter))
        result = -1;
    errno = 0;
    sample->nice_value = getpriority(PRIO_PROCESS, 0);
    if (errno || sample->nice_value != 19 || sample->policy != SCHED_OTHER ||
        parameter.sched_priority != 0)
        result = -1;
    pthread_setname_np(pthread_self(), "spo-sampler");
    pthread_mutex_lock(&sample->mutex);
    sample->ready = result ? -1 : 1;
    pthread_cond_signal(&sample->condition);
    pthread_mutex_unlock(&sample->mutex);
    if (result)
        return NULL;
    while (!atomic_load(&sample->stopping)) {
        sample_once(sample, spo_cpu_now());
        pthread_mutex_lock(&sample->mutex);
        while (!atomic_load(&sample->stopping) && spo_now() < sample->next_cycle) {
            struct timespec until = {(time_t)(sample->next_cycle / UINT64_C(1000000000)),
                                     (long)(sample->next_cycle % UINT64_C(1000000000))};
            pthread_cond_timedwait(&sample->condition, &sample->mutex, &until);
        }
        pthread_mutex_unlock(&sample->mutex);
    }
    publish(sample, 0, 1);
    return NULL;
}
#endif
spo_result spo_metrics_init(void) {
    spo_sampler *sample = calloc(1, sizeof(*sample));
    if (!sample)
        return SPO_CAPACITY;
    spo->sampler = sample;
    atomic_init(&sample->enabled, spo->overlay.items);
    atomic_init(&sample->generation, 0);
    atomic_init(&sample->stopping, 0);
    atomic_init(&sample->sampling, 0);
    atomic_init(&sample->sequence, 0);
    for (unsigned i = 0; i < SPO_PUB_WORDS; i++)
        atomic_init(&sample->words[i], 0);
    sample->nice_value = INT32_MIN;
    sample->policy = -1;
    sample->started_ns = spo_now();
    spo_readers_init(sample);
    for (unsigned i = 0; i < sample->source_count; i++)
        sample->available |= sample->sources[i].needs;
    spo->available_items = sample->available;
    spo->requested_items = spo->overlay.items;
    spo->sampling_stats.struct_size = sizeof(spo_sampling_stats);
    spo->sampling_stats.source_count = sample->source_count;
    spo->sampling_stats.nice_value = INT32_MIN;
    spo->sampling_stats.scheduler_policy = -1;
    publish(sample, 0, 0);
    if (spo->config.flags & SPO_INIT_NO_SAMPLER_THREAD)
        return SPO_OK;
#if defined(__linux__)
    pthread_condattr_t condition;
    pthread_attr_t attributes;
    struct sched_param parameter = {0};
    if (pthread_mutex_init(&sample->mutex, NULL))
        goto fail;
    sample->mutex_ready = 1;
    if (pthread_condattr_init(&condition))
        goto fail;
    int error = pthread_condattr_setclock(&condition, CLOCK_MONOTONIC);
    if (!error)
        error = pthread_cond_init(&sample->condition, &condition);
    pthread_condattr_destroy(&condition);
    if (error)
        goto fail;
    sample->condition_ready = 1;
    if (pthread_attr_init(&attributes))
        goto fail;
    size_t stack = 128 * 1024;
    if (stack < (size_t)PTHREAD_STACK_MIN)
        stack = (size_t)PTHREAD_STACK_MIN;
    error = pthread_attr_setstacksize(&attributes, stack);
    if (!error)
        error = pthread_attr_setschedpolicy(&attributes, SCHED_OTHER);
    if (!error)
        error = pthread_attr_setschedparam(&attributes, &parameter);
    if (!error)
        error = pthread_attr_setinheritsched(&attributes, PTHREAD_EXPLICIT_SCHED);
    if (!error) {
        sample->thread_started = 1;
        error = pthread_create(&sample->thread, &attributes, run, sample);
        if (error)
            sample->thread_started = 0;
    }
    pthread_attr_destroy(&attributes);
    if (error)
        goto fail;
    pthread_mutex_lock(&sample->mutex);
    while (!sample->ready)
        pthread_cond_wait(&sample->condition, &sample->mutex);
    int ready = sample->ready;
    pthread_mutex_unlock(&sample->mutex);
    if (ready != 1)
        goto fail;
    spo->sampling_stats.threaded = 1;
    spo->sampling_stats.nice_value = sample->nice_value;
    spo->sampling_stats.scheduler_policy = sample->policy;
    return SPO_OK;
#endif
#if defined(__linux__)
fail:
#endif
    spo_metrics_close();
    return SPO_THREAD_UNAVAILABLE;
}
void spo_metrics_close(void) {
    spo_sampler *sample = spo->sampler;
    if (!sample)
        return;
    if (sample->thread_started) {
        atomic_store(&sample->stopping, 1);
        pthread_mutex_lock(&sample->mutex);
        pthread_cond_signal(&sample->condition);
        pthread_mutex_unlock(&sample->mutex);
        pthread_join(sample->thread, NULL);
    }
    if (sample->condition_ready)
        pthread_cond_destroy(&sample->condition);
    if (sample->mutex_ready)
        pthread_mutex_destroy(&sample->mutex);
    spo_readers_close(sample);
    free(sample);
    spo->sampler = NULL;
}
spo_result spo_sample(void) {
    spo_context *context = spo;
    if (!context)
        return SPO_NOT_INITIALIZED;
    if (!(context->config.flags & SPO_INIT_NO_SAMPLER_THREAD))
        return SPO_INVALID;
    spo_sampler *sample = context->sampler;
    unsigned expected = 0;
    uint64_t start = spo_cpu_now();
    if (!atomic_compare_exchange_strong(&sample->sampling, &expected, 1))
        return SPO_BUSY;
    sample_once(sample, start);
    atomic_store(&sample->sampling, 0);
    return SPO_OK;
}
void spo_metrics_request(void) {
    if (spo->sampler && spo->requested_items != spo->overlay.items) {
        spo->requested_items = spo->overlay.items;
        atomic_store(&spo->sampler->enabled, spo->requested_items);
    }
}
void spo_metrics_reset(void) {
    if (spo->sampler)
        atomic_fetch_add(&spo->sampler->generation, 1);
}
int spo_metrics_available(int item) {
    return !!(spo->available_items & SPO_BIT(item));
}
void spo_metrics_collect(uint64_t now) {
    spo_sampler *sample = spo->sampler;
    uint32_t words[SPO_PUB_WORDS], sequence = atomic_load(&sample->sequence), i;
    if (!(sequence & 1) && sequence != spo->sampling_sequence) {
        for (i = 0; i < SPO_PUB_WORDS; i++)
            words[i] = atomic_load(&sample->words[i]);
        if (atomic_load(&sample->sequence) != sequence)
            goto stale;
        spo->sampling_sequence = sequence;
        for (i = 0; i < SPO_ITEM_COUNT; i++) {
            uint64_t bits = unpack(words, i * 4);
            memcpy(&spo->metrics[i].value, &bits, sizeof(bits));
            spo->metrics[i].stamp = unpack(words, i * 4 + 2);
            spo->metrics[i].valid = !!(words[SPO_PUB_VALID] & SPO_BIT(i));
        }
        spo->available_items = words[SPO_PUB_AVAILABLE];
        spo->battery_charging = (int)words[SPO_PUB_CHARGING];
        spo_sampling_stats *out = &spo->sampling_stats;
        out->cpu_ns_total = unpack(words, SPO_PUB_CPU);
        out->elapsed_ns = unpack(words, SPO_PUB_ELAPSED);
        out->cycles = unpack(words, SPO_PUB_CYCLES);
        out->reads = unpack(words, SPO_PUB_READS);
        out->cycle_cpu_ns_max = unpack(words, SPO_PUB_MAX);
        out->threaded = !(spo->config.flags & SPO_INIT_NO_SAMPLER_THREAD);
        out->source_count = sample->source_count;
        out->nice_value = sample->nice_value;
        out->scheduler_policy = sample->policy;
        if (out->elapsed_ns - spo->cost_sampler_elapsed >= UINT64_C(500000000)) {
            out->cpu_percent = 100.0 * (double)(out->cpu_ns_total - spo->cost_sampler_cpu) /
                               (double)(out->elapsed_ns - spo->cost_sampler_elapsed);
            spo->cost_sampler_cpu = out->cpu_ns_total;
            spo->cost_sampler_elapsed = out->elapsed_ns;
        }
        spo_metrics_cost();
    }
stale:
    for (i = SPO_SYSTEM_CPU; i < SPO_ITEM_COUNT; i++)
        if (spo->metrics[i].valid && now - spo->metrics[i].stamp > UINT64_C(1500000000))
            spo->metrics[i].valid = 0;
}
void spo_metrics_cost(void) {
    spo_sampling_stats *out = &spo->sampling_stats;
    if (spo->stats.sample_frames &&
        (out->elapsed_ns >= UINT64_C(500000000) || (!out->threaded && !out->cycles))) {
        out->sample_frames = spo->stats.sample_frames;
        out->cpu_us_per_frame = out->cpu_percent * spo->cost_frame_ns / 100000.0;
        out->total_cpu_us_per_frame = spo->stats.cpu_us_mean + out->cpu_us_per_frame;
        out->total_cpu_percent = spo->cost_render_percent + out->cpu_percent;
    }
}
spo_result spo_get_sampling_stats(spo_sampling_stats *out) {
    if (!spo)
        return SPO_NOT_INITIALIZED;
    if (!out || out->struct_size != sizeof(*out))
        return spo_fail(SPO_ABI);
    spo_metrics_collect(spo_now());
    *out = spo->sampling_stats;
    return SPO_OK;
}
