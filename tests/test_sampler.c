/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "spo.h"
#include <assert.h>
#include <dirent.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

static pthread_t render;
static atomic_int in_frame, frame_reads, frame_locks, block_read, entered_read, release_read;
static atomic_int check_rate, rate_errors;
static uint64_t last_read[4096];
static uint64_t monotonic_ns(void) {
    struct timespec time;
    assert(!clock_gettime(CLOCK_MONOTONIC, &time));
    return (uint64_t)time.tv_sec * UINT64_C(1000000000) + (uint64_t)time.tv_nsec;
}
static void delay(void) {
    struct timespec time = {0, 1000000};
    nanosleep(&time, NULL);
}
ssize_t __real_pread(int, void *, size_t, off_t);
ssize_t __wrap_pread(int fd, void *data, size_t length, off_t offset) {
    if (pthread_equal(pthread_self(), render) && atomic_load(&in_frame))
        atomic_fetch_add(&frame_reads, 1);
    if (!pthread_equal(pthread_self(), render)) {
        if (atomic_load(&check_rate) && fd >= 0 && fd < 4096) {
            uint64_t now = monotonic_ns();
            if (last_read[fd] && now - last_read[fd] < UINT64_C(1000000000))
                atomic_fetch_add(&rate_errors, 1);
            last_read[fd] = now;
        }
        if (atomic_load(&block_read)) {
            atomic_store(&entered_read, 1);
            while (!atomic_load(&release_read))
                delay();
        }
    }
    return __real_pread(fd, data, length, offset);
}
int __real_pthread_mutex_lock(pthread_mutex_t *);
int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex) {
    if (pthread_equal(pthread_self(), render) && atomic_load(&in_frame))
        atomic_fetch_add(&frame_locks, 1);
    return __real_pthread_mutex_lock(mutex);
}
static unsigned entries(const char *path) {
    unsigned count = 0;
    DIR *directory = opendir(path);
    struct dirent *entry;
    assert(directory);
    while ((entry = readdir(directory)))
        if (entry->d_name[0] != '.')
            count++;
    closedir(directory);
    return count;
}
static void resources(unsigned threads, unsigned descriptors) {
    uint64_t until = monotonic_ns() + UINT64_C(100000000);
    while (entries("/proc/self/task") != threads && monotonic_ns() < until)
        delay();
    assert(entries("/proc/self/task") == threads);
    assert(entries("/proc/self/fd") == descriptors);
}
static void frame(void) {
    atomic_store(&in_frame, 1);
    assert(spo_frame(640, 480) == SPO_OK);
    atomic_store(&in_frame, 0);
}
static spo_config config(uint32_t flags) {
    spo_config out = {0};
    out.struct_size = sizeof(out);
    out.abi_version = SPO_ABI_VERSION;
    out.flags = flags | SPO_INIT_NO_RENDER;
    return out;
}
static void *manual(void *unused) {
    (void)unused;
    assert(spo_sample() == SPO_OK);
    return NULL;
}
static void *warmup(void *user) {
    return user;
}
int main(void) {
    render = pthread_self();
    pthread_t warm;
    assert(!pthread_create(&warm, NULL, warmup, NULL));
    assert(!pthread_join(warm, NULL));
    for (int i = 0; i < 10; i++)
        delay();
    unsigned threads = entries("/proc/self/task"), descriptors = entries("/proc/self/fd");
    int original_nice = getpriority(PRIO_PROCESS, 0);
    spo_sampling_stats stats = {0};
    stats.struct_size = sizeof(stats);
    assert(spo_sample() == SPO_NOT_INITIALIZED);
    spo_config automatic = config(0);
    assert(spo_init(&automatic) == SPO_OK);
    assert(entries("/proc/self/task") == threads + 1);
    assert(getpriority(PRIO_PROCESS, 0) == original_nice);
    assert(spo_get_sampling_stats(&stats) == SPO_OK);
    assert(stats.threaded && stats.nice_value == 19 && stats.scheduler_policy == SCHED_OTHER);
    assert(stats.source_count >= 3);
    assert(spo_sample() == SPO_INVALID);
    atomic_store(&check_rate, 1);
    atomic_store(&block_read, 1);
    assert(spo_preset_set(4) == SPO_OK);
    uint64_t until = monotonic_ns() + UINT64_C(5000000000);
    while (!atomic_load(&entered_read) && monotonic_ns() < until)
        delay();
    assert(atomic_load(&entered_read));
    for (int i = 0; i < 100; i++)
        frame();
    assert(!atomic_load(&frame_reads) && !atomic_load(&frame_locks));
    atomic_store(&release_read, 1);
    atomic_store(&block_read, 0);
    uint64_t start = monotonic_ns();
    while (monotonic_ns() - start < UINT64_C(2200000000)) {
        frame();
        assert(spo_get_sampling_stats(&stats) == SPO_OK);
        assert(stats.cpu_ns_total <= stats.elapsed_ns);
        delay();
    }
    assert(stats.reads >= stats.source_count && stats.cpu_ns_total > 0);
    assert(!atomic_load(&rate_errors));
    spo_shutdown();
    resources(threads, descriptors);

    atomic_store(&check_rate, 0);
    atomic_store(&block_read, 1);
    atomic_store(&entered_read, 0);
    atomic_store(&release_read, 0);
    spo_config external = config(SPO_INIT_NO_SAMPLER_THREAD);
    assert(spo_init(&external) == SPO_OK);
    assert(entries("/proc/self/task") == threads);
    assert(spo_preset_set(4) == SPO_OK);
    pthread_t host_worker;
    assert(!pthread_create(&host_worker, NULL, manual, NULL));
    until = monotonic_ns() + UINT64_C(5000000000);
    while (!atomic_load(&entered_read) && monotonic_ns() < until)
        delay();
    assert(atomic_load(&entered_read));
    assert(spo_sample() == SPO_BUSY);
    frame();
    atomic_store(&release_read, 1);
    assert(!pthread_join(host_worker, NULL));
    assert(spo_get_sampling_stats(&stats) == SPO_OK && !stats.threaded && stats.reads);
    assert(!atomic_load(&frame_reads) && !atomic_load(&frame_locks));
    spo_shutdown();
    resources(threads, descriptors);
    for (int i = 0; i < 8; i++) {
        assert(spo_init(&automatic) == SPO_OK);
        spo_shutdown();
    }
    resources(threads, descriptors);
    puts("sampler: one nice-19 SCHED_OTHER worker, joined shutdown, manual mode, bounded reads and "
         "lock-free frames passed");
    return 0;
}
