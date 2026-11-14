/* SPDX-License-Identifier: MIT */
#include "mali.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif
const char *spo_mali_device = "/dev/mali0";
#define JM_GPU_ACTIVE 6
#define BLOCK_BYTES 256
uint32_t spo_mali_jm_active(const uint8_t *block) {
    uint32_t v;
    memcpy(&v, block + JM_GPU_ACTIVE * 4, sizeof(v));
    return v;
}
int spo_mali_gpu_supported(uint32_t gpu_id) {
    switch ((gpu_id >> 16) & 0xf00f) {
    case 0x6000:
    case 0x6001:
    case 0x7000:
    case 0x7001:
    case 0x7002:
    case 0x7003:
    case 0x9000:
    case 0x9001:
    case 0x9003:
        return 1;
    default:
        return 0;
    }
}
int spo_gpu_load_percent(uint64_t active, double clock_mhz, uint64_t elapsed_ns, double *out) {
    double cycles, load;
    if (!(clock_mhz > 0) || !isfinite(clock_mhz) || !elapsed_ns)
        return 0;
    cycles = clock_mhz * 1e-3 * (double)elapsed_ns;
    load = 100.0 * (double)active / cycles;
    *out = load < 0 ? 0 : load > 100 ? 100 : load;
    return 1;
}
#if defined(__linux__)
/* kbase job manager ioctl interface (version 11.x) and the hwcnt reader file it returns. */
typedef struct {
    uint16_t major, minor;
} version_check_args;
typedef struct {
    uint64_t buffer;
    uint32_t size, flags;
} gpuprops_args;
typedef struct {
    uint32_t buffer_count, jm_bm, shader_bm, geometry_bm, mmu_l2_bm;
} reader_setup_args;
typedef struct {
    uint64_t timestamp;
    uint32_t event_id, buffer_idx;
} reader_metadata;
typedef struct {
    reader_metadata metadata;
    uint64_t top, shader_cores;
} reader_metadata_cycles;
typedef struct {
    uint32_t version, features;
} reader_api_version;
enum {
    PROP_RAW_GPU_ID = 55,
    PROPS_MAX = 4096,
    CONTEXT_SUBMIT_DISABLED = 1u << 1,
    FEATURE_CYCLES_TOP = 1u << 0,
    FEATURE_CYCLES_SHADER = 1u << 1,
    BUFFERS = 4
};
#define IOCTL_VERSION_CHECK _IOWR(0x80, 0x0, version_check_args)
#define IOCTL_SET_FLAGS _IOW(0x80, 0x1, uint32_t)
#define IOCTL_GET_PROPS _IOW(0x80, 0x3, gpuprops_args)
#define IOCTL_READER_SETUP _IOW(0x80, 0x8, reader_setup_args)
#define IOCTL_GET_BUFFER_SIZE _IOR(0xbe, 0x1, uint32_t)
#define IOCTL_DUMP _IOW(0xbe, 0x10, uint32_t)
#define IOCTL_CLEAR _IOW(0xbe, 0x11, uint32_t)
#define IOCTL_GET_BUFFER _IOR(0xbe, 0x20, reader_metadata)
#define IOCTL_GET_BUFFER_CYCLES _IOR(0xbe, 0x20, reader_metadata_cycles)
#define IOCTL_PUT_BUFFER _IOW(0xbe, 0x21, reader_metadata)
#define IOCTL_PUT_BUFFER_CYCLES _IOW(0xbe, 0x21, reader_metadata_cycles)
#define IOCTL_API_VERSION _IOW(0xbe, 0xff, uint32_t)
#define IOCTL_API_VERSION_FEATURES _IOW(0xbe, 0xff, reader_api_version)
struct spo_mali {
    int device, reader;
    void *map;
    size_t map_size, buffer_size;
    int cycles;
};
static int ioctl_retry(int fd, unsigned long request, void *arg) {
    int r;
    do
        r = ioctl(fd, request, arg);
    while (r < 0 && errno == EINTR);
    return r;
}
void spo_mali_close(spo_mali *mali) {
    if (!mali)
        return;
    if (mali->map && mali->map != MAP_FAILED)
        munmap(mali->map, mali->map_size);
    if (mali->reader >= 0)
        close(mali->reader);
    if (mali->device >= 0)
        close(mali->device);
    free(mali);
}
/* Finds raw_gpu_id in the packed key/value property list from the GET_GPUPROPS ioctl. */
static int props_gpu_id(const uint8_t *data, size_t size, uint32_t *id) {
    size_t at = 0;
    while (at + 4 <= size) {
        uint32_t key, value = 0;
        size_t width;
        memcpy(&key, data + at, sizeof(key));
        width = (size_t)1 << (key & 3);
        at += 4;
        if (at + width > size)
            return 0;
        if ((key >> 2) == PROP_RAW_GPU_ID && width == 4) {
            memcpy(&value, data + at, sizeof(value));
            *id = value;
            return 1;
        }
        at += width;
    }
    return 0;
}
spo_mali *spo_mali_open(const char *path) {
    spo_mali *mali = calloc(1, sizeof(*mali));
    version_check_args version = {0, 0};
    uint32_t flags = CONTEXT_SUBMIT_DISABLED;
    gpuprops_args props;
    reader_setup_args setup;
    reader_api_version api;
    uint8_t *props_data = NULL;
    uint32_t size = 0, gpu_id = 0;
    int needed;
    if (!mali)
        return NULL;
    mali->device = mali->reader = -1;
    mali->device = open(path, O_RDWR | O_CLOEXEC);
    if (mali->device < 0)
        goto fail;
    if (ioctl_retry(mali->device, IOCTL_VERSION_CHECK, &version) || version.major != 11 ||
        version.minor >= 34)
        goto fail;
    ioctl_retry(mali->device, IOCTL_SET_FLAGS, &flags);
    memset(&props, 0, sizeof(props));
    needed = ioctl_retry(mali->device, IOCTL_GET_PROPS, &props);
    if (needed <= 0 || needed > PROPS_MAX)
        goto fail;
    props_data = malloc((size_t)needed);
    if (!props_data)
        goto fail;
    props.buffer = (uint64_t)(uintptr_t)props_data;
    props.size = (uint32_t)needed;
    needed = ioctl_retry(mali->device, IOCTL_GET_PROPS, &props);
    if (needed <= 0 || needed > PROPS_MAX || !props_gpu_id(props_data, (size_t)needed, &gpu_id) ||
        !spo_mali_gpu_supported(gpu_id))
        goto fail;
    free(props_data);
    props_data = NULL;
    memset(&setup, 0, sizeof(setup));
    setup.buffer_count = BUFFERS;
    setup.jm_bm = 1u << (JM_GPU_ACTIVE / 4);
    mali->reader = ioctl_retry(mali->device, IOCTL_READER_SETUP, &setup);
    if (mali->reader < 0)
        goto fail;
    memset(&api, 0, sizeof(api));
    if (ioctl_retry(mali->reader, IOCTL_API_VERSION_FEATURES, &api)) {
        memset(&api, 0, sizeof(api));
        if (ioctl_retry(mali->reader, IOCTL_API_VERSION, &api.version))
            goto fail;
    }
    mali->cycles = !!(api.features & (FEATURE_CYCLES_TOP | FEATURE_CYCLES_SHADER));
    if (ioctl_retry(mali->reader, IOCTL_GET_BUFFER_SIZE, &size) || size < BLOCK_BYTES ||
        size > (1u << 20))
        goto fail;
    mali->buffer_size = size;
    mali->map_size = (size_t)size * BUFFERS;
    mali->map = mmap(NULL, mali->map_size, PROT_READ, MAP_PRIVATE, mali->reader, 0);
    if (mali->map == MAP_FAILED)
        goto fail;
    ioctl_retry(mali->reader, IOCTL_CLEAR, NULL);
    return mali;
fail:
    free(props_data);
    spo_mali_close(mali);
    return NULL;
}
int spo_mali_dump(spo_mali *mali, uint64_t *active, uint64_t *stamp_ns) {
    reader_metadata_cycles meta;
    struct pollfd wait = {mali->reader, POLLIN, 0};
    int got;
    memset(&meta, 0, sizeof(meta));
    if (ioctl_retry(mali->reader, IOCTL_DUMP, NULL))
        return 0;
    if (poll(&wait, 1, 100) != 1 || !(wait.revents & POLLIN))
        return 0;
    got = mali->cycles ? ioctl_retry(mali->reader, IOCTL_GET_BUFFER_CYCLES, &meta)
                       : ioctl_retry(mali->reader, IOCTL_GET_BUFFER, &meta.metadata);
    if (got)
        return 0;
    if (meta.metadata.buffer_idx < BUFFERS) {
        *active = spo_mali_jm_active((const uint8_t *)mali->map +
                                     mali->buffer_size * meta.metadata.buffer_idx);
        *stamp_ns = meta.metadata.timestamp;
        got = 1;
    } else
        got = 0;
    if (mali->cycles)
        ioctl_retry(mali->reader, IOCTL_PUT_BUFFER_CYCLES, &meta);
    else
        ioctl_retry(mali->reader, IOCTL_PUT_BUFFER, &meta.metadata);
    return got;
}
#else
spo_mali *spo_mali_open(const char *path) {
    (void)path;
    return NULL;
}
int spo_mali_dump(spo_mali *mali, uint64_t *active, uint64_t *stamp_ns) {
    (void)mali;
    (void)active;
    (void)stamp_ns;
    return 0;
}
void spo_mali_close(spo_mali *mali) {
    (void)mali;
}
#endif
