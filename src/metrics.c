/* SPDX-License-Identifier: MIT */
#include "sampling.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { CPU, PROCESS, MEMORY, GPU_LOAD, CPU_CLOCK, GPU_CLOCK, TEMP, BATTERY, CHARGING };
static int u64(const char *text, uint64_t *out, const char **after) {
    char *end;
    unsigned long long n;
    while (isspace((unsigned char)*text))
        text++;
    if (!isdigit((unsigned char)*text))
        return 0;
    errno = 0;
    n = strtoull(text, &end, 10);
    if (errno || end == text)
        return 0;
    *out = (uint64_t)n;
    if (after)
        *after = end;
    return 1;
}
int spo_parse_cpu(const char *text, uint64_t *busy, uint64_t *total) {
    uint64_t values[8], sum = 0;
    int i;
    const char *p = text;
    if (strncmp(p, "cpu ", 4))
        return 0;
    p += 4;
    for (i = 0; i < 8; i++) {
        if (!u64(p, &values[i], &p) || UINT64_MAX - sum < values[i])
            return 0;
        sum += values[i];
    }
    *total = sum;
    *busy = sum - values[3] - values[4];
    return 1;
}
int spo_parse_process(const char *text, uint64_t *ticks, uint64_t *rss) {
    const char *p = strrchr(text, ')');
    int field;
    uint64_t user = 0, kernel = 0;
    if (!p || !strchr(text, '(') || p[1] != ' ')
        return 0;
    p += 2;
    for (field = 3; field <= 24; field++) {
        const char *start = p;
        uint64_t value;
        while (*p && !isspace((unsigned char)*p))
            p++;
        if (p == start)
            return 0;
        if (field == 14 || field == 15 || field == 24) {
            const char *end;
            if (!u64(start, &value, &end) || end != p)
                return 0;
            if (field == 14)
                user = value;
            else if (field == 15)
                kernel = value;
            else
                *rss = value;
        }
        while (*p == ' ' || *p == '\t')
            p++;
    }
    if (UINT64_MAX - user < kernel)
        return 0;
    *ticks = user + kernel;
    return 1;
}
int spo_parse_mem(const char *text, double *out) {
    const char *p = text, *end;
    uint64_t value;
    while (p && *p) {
        if (!strncmp(p, "MemAvailable:", 13)) {
            if (!u64(p + 13, &value, &end))
                return 0;
            while (*end == ' ' || *end == '\t')
                end++;
            if (strncmp(end, "kB", 2))
                return 0;
            end += 2;
            while (*end == ' ' || *end == '\t' || *end == '\r')
                end++;
            if (*end && *end != '\n')
                return 0;
            *out = (double)value / 1024.0;
            return 1;
        }
        p = strchr(p, '\n');
        if (p)
            p++;
    }
    return 0;
}
int spo_parse_number(const char *text, double *out) {
    char *end;
    double value;
    while (isspace((unsigned char)*text))
        text++;
    if (!*text)
        return 0;
    errno = 0;
    value = strtod(text, &end);
    if (errno || end == text || !isfinite(value))
        return 0;
    while (isspace((unsigned char)*end))
        end++;
    if (*end)
        return 0;
    *out = value;
    return 1;
}
static int source(spo_sampler *sample, const char *path, int kind, uint32_t needs) {
    int fd;
    uint32_t i;
    for (i = 0; i < sample->source_count; i++)
        if (sample->sources[i].kind == kind)
            return 1;
    if (sample->source_count == SPO_FDS)
        return 0;
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 0;
    sample->sources[sample->source_count++] = (spo_source){fd, kind, needs, 0, 0};
    return 1;
}
static void first_glob(spo_sampler *sample, const char *pattern, int kind, uint32_t needs) {
    glob_t matches;
    size_t i;
    memset(&matches, 0, sizeof(matches));
    if (!glob(pattern, 0, NULL, &matches))
        for (i = 0; i < matches.gl_pathc; i++)
            if (source(sample, matches.gl_pathv[i], kind, needs))
                break;
    globfree(&matches);
}
static int read_init(const char *path, char *buf, size_t len) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    ssize_t n;
    if (fd < 0)
        return 0;
    n = pread(fd, buf, len - 1, 0);
    close(fd);
    if (n <= 0 || (size_t)n >= len - 1)
        return 0;
    buf[n] = 0;
    return 1;
}
int spo_parse_gpu_load(const char *text, double *out) {
    char *end;
    double value;
    errno = 0;
    value = strtod(text, &end);
    if (end == text || errno || !isfinite(value) || value < 0 || value > 100)
        return 0;
    while (isspace((unsigned char)*end))
        end++;
    if (*end == '%')
        end++;
    else if (*end == '@') {
        char *after;
        double clock;
        errno = 0;
        clock = strtod(end + 1, &after);
        if (after == end + 1 || errno || !isfinite(clock) || clock < 0 || strncmp(after, "Hz", 2))
            return 0;
        end = after + 2;
    }
    while (isspace((unsigned char)*end))
        end++;
    if (*end)
        return 0;
    *out = value;
    return 1;
}
void spo_readers_init(spo_sampler *sample) {
    glob_t matches;
    size_t i;
    char path[4096], type[256];
    sample->ticks = sysconf(_SC_CLK_TCK);
    sample->page_size = sysconf(_SC_PAGESIZE);
    if (sample->ticks <= 0)
        sample->ticks = 100;
    if (sample->page_size <= 0)
        sample->page_size = 4096;
#if defined(__linux__)
    source(sample, "/proc/stat", CPU, SPO_BIT(SPO_SYSTEM_CPU));
    source(sample, "/proc/self/stat", PROCESS, SPO_BIT(SPO_PROCESS_CPU) | SPO_BIT(SPO_RSS));
    source(sample, "/proc/meminfo", MEMORY, SPO_BIT(SPO_MEM_AVAILABLE));
    if (!source(sample, "/sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq", CPU_CLOCK,
                SPO_BIT(SPO_CPU_CLOCK)))
        source(sample, "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", CPU_CLOCK,
               SPO_BIT(SPO_CPU_CLOCK));
    first_glob(sample, "/sys/class/devfreq/*gpu*/cur_freq", GPU_CLOCK, SPO_BIT(SPO_GPU_CLOCK));
    first_glob(sample, "/sys/class/devfreq/*gpu*/load", GPU_LOAD, SPO_BIT(SPO_GPU_LOAD));
    first_glob(sample, "/sys/class/devfreq/*gpu*/device/utilization", GPU_LOAD,
               SPO_BIT(SPO_GPU_LOAD));
    first_glob(sample, "/sys/class/devfreq/*mali*/cur_freq", GPU_CLOCK, SPO_BIT(SPO_GPU_CLOCK));
    first_glob(sample, "/sys/class/devfreq/*mali*/load", GPU_LOAD, SPO_BIT(SPO_GPU_LOAD));
    memset(&matches, 0, sizeof(matches));
    if (!glob("/sys/class/thermal/thermal_zone*", 0, NULL, &matches))
        for (i = 0; i < matches.gl_pathc; i++) {
            snprintf(path, sizeof(path), "%s/type", matches.gl_pathv[i]);
            if (!read_init(path, type, sizeof(type)))
                continue;
            if (!strstr(type, "soc") && !strstr(type, "cpu") && !strstr(type, "CPU"))
                continue;
            snprintf(path, sizeof(path), "%s/temp", matches.gl_pathv[i]);
            if (source(sample, path, TEMP, SPO_BIT(SPO_TEMPERATURE)))
                break;
        }
    globfree(&matches);
    memset(&matches, 0, sizeof(matches));
    if (!glob("/sys/class/power_supply/*", 0, NULL, &matches))
        for (i = 0; i < matches.gl_pathc; i++) {
            snprintf(path, sizeof(path), "%s/type", matches.gl_pathv[i]);
            if (!read_init(path, type, sizeof(type)) || strncmp(type, "Battery", 7))
                continue;
            snprintf(path, sizeof(path), "%s/capacity", matches.gl_pathv[i]);
            if (source(sample, path, BATTERY, SPO_BIT(SPO_BATTERY))) {
                snprintf(path, sizeof(path), "%s/status", matches.gl_pathv[i]);
                source(sample, path, CHARGING, SPO_BIT(SPO_CHARGING));
                break;
            }
        }
    globfree(&matches);
#else
    (void)matches;
    (void)i;
    (void)path;
    (void)type;
    (void)source;
    (void)first_glob;
    (void)read_init;
#endif
}
void spo_readers_close(spo_sampler *sample) {
    uint32_t i;
    for (i = 0; i < sample->source_count; i++)
        close(sample->sources[i].fd);
}
static void metric(spo_sampler *sample, int id, double value, uint64_t now) {
    sample->live[id] = (spo_metric){value, now, 1};
}
static int decode(spo_sampler *sample, spo_source *s, const char *text, uint64_t now) {
    double v;
    uint64_t a, b;
    if (s->kind == CPU) {
        if (!spo_parse_cpu(text, &a, &b))
            return 0;
        if (sample->cpu_baseline && b > sample->cpu_previous[1] && a >= sample->cpu_previous[0] &&
            a - sample->cpu_previous[0] <= b - sample->cpu_previous[1])
            metric(sample, SPO_SYSTEM_CPU,
                   100.0 * (double)(a - sample->cpu_previous[0]) /
                       (double)(b - sample->cpu_previous[1]),
                   now);
        else
            sample->live[SPO_SYSTEM_CPU].valid = 0;
        sample->cpu_previous[0] = a;
        sample->cpu_previous[1] = b;
        sample->cpu_baseline = 1;
        return 1;
    }
    if (s->kind == PROCESS) {
        if (!spo_parse_process(text, &a, &b))
            return 0;
        metric(sample, SPO_RSS, (double)b * (double)sample->page_size / 1048576.0, now);
        if (sample->process_baseline && a >= sample->process_previous &&
            now > sample->process_stamp) {
#ifdef _SC_NPROCESSORS_ONLN
            long cores = sysconf(_SC_NPROCESSORS_ONLN);
#else
            long cores = 1;
#endif
            metric(sample, SPO_PROCESS_CPU,
                   100.0 * (double)(a - sample->process_previous) / (double)sample->ticks * 1e9 /
                       (double)(now - sample->process_stamp) / (double)(cores > 0 ? cores : 1),
                   now);
        }
        else
            sample->live[SPO_PROCESS_CPU].valid = 0;
        sample->process_previous = a;
        sample->process_stamp = now;
        sample->process_baseline = 1;
        return 1;
    }
    if (s->kind == MEMORY) {
        if (!spo_parse_mem(text, &v))
            return 0;
        metric(sample, SPO_MEM_AVAILABLE, v, now);
        return 1;
    }
    if (s->kind == CHARGING) {
        sample->charging = !strncmp(text, "Charging", 8);
        metric(sample, SPO_CHARGING, sample->charging, now);
        return 1;
    }
    if (s->kind == GPU_LOAD) {
        if (!spo_parse_gpu_load(text, &v))
            return 0;
        metric(sample, SPO_GPU_LOAD, v, now);
        return 1;
    }
    if (!spo_parse_number(text, &v) || v < 0)
        return 0;
    if (s->kind == CPU_CLOCK)
        metric(sample, SPO_CPU_CLOCK, v / 1000.0, now);
    else if (s->kind == GPU_CLOCK)
        metric(sample, SPO_GPU_CLOCK, v / 1000000.0, now);
    else if (s->kind == TEMP) {
        if (v > 200000)
            return 0;
        metric(sample, SPO_TEMPERATURE, v / 1000.0, now);
    } else if (s->kind == BATTERY) {
        if (v > 100)
            return 0;
        metric(sample, SPO_BATTERY, v, now);
    }
    return 1;
}
void spo_readers_poll(spo_sampler *sample, uint64_t now, uint32_t mask) {
    uint32_t i, generation = atomic_load(&sample->generation);
    if (generation != sample->seen_generation || mask != sample->last_mask) {
        sample->cpu_baseline = sample->process_baseline = 0;
        sample->seen_generation = generation;
        sample->last_mask = mask;
    }
    sample->available = 0;
    for (i = 0; i < sample->source_count; i++) {
        spo_source *s = &sample->sources[i];
        ssize_t n;
        uint64_t read_now = spo_now();
        if (s->failures < 3 && (s->needs & mask) && read_now >= s->due) {
            s->due = read_now + UINT64_C(1000000000);
            n = pread(s->fd, sample->read_buffer, sizeof(sample->read_buffer) - 1, 0);
            sample->reads++;
            if (n > 0 && (size_t)n < sizeof(sample->read_buffer) - 1) {
                sample->read_buffer[n] = 0;
                if (decode(sample, s, sample->read_buffer, spo_now()))
                    s->failures = 0;
                else
                    s->failures++;
            } else
                s->failures++;
            if (s->failures) {
                for (uint32_t item = 0; item < SPO_ITEM_COUNT; item++)
                    if (s->needs & SPO_BIT(item))
                        sample->live[item].valid = 0;
                if (s->kind == CHARGING)
                    sample->charging = 0;
            }
        }
        if (s->failures < 3)
            sample->available |= s->needs;
    }
    (void)now;
}
