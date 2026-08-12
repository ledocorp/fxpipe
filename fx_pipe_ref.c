/* fxpipe — parallel BLAKE3 under FsCap via host nursery workers. */
#include "blake3.h"
#include "fx_cap_runtime.h"
#include "fx_task_nursery.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <stdatomic.h>
#endif

#define FX_PIPE_OK 0
#define FX_PIPE_USAGE (-1)
#define FX_PIPE_DENY (-2)
#define FX_PIPE_FAIL (-3)
#define FX_PIPE_MAX_JOBS 256
#define FX_PIPE_HEX_LEN (BLAKE3_OUT_LEN * 2)

typedef struct {
    char *path;
    char hex[FX_PIPE_HEX_LEN + 1];
    int32_t status; /* 0 ok, -5 deny, -6 missing, -1 err */
} FxPipeJob;

static FxPipeJob g_jobs[FX_PIPE_MAX_JOBS];
static int g_job_n = 0;
static FxFsCap *g_fs = NULL;

#ifdef _WIN32
static volatile LONG g_next = 0;
#else
static atomic_int g_next;
#endif

static void to_hex(const uint8_t *in, size_t n, char *out) {
    static const char *digits = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; i++) {
        out[i * 2] = digits[(in[i] >> 4) & 0xf];
        out[i * 2 + 1] = digits[in[i] & 0xf];
    }
    out[n * 2] = '\0';
}

static char *read_all(const char *path, size_t *out_len) {
    FILE *f;
    long sz;
    char *buf;
    size_t n;

    f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (char *)malloc((size_t)sz + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (out_len != NULL) {
        *out_len = n;
    }
    return buf;
}

static void hash_job(FxPipeJob *job) {
    char *raw;
    size_t n;
    blake3_hasher h;
    uint8_t out[BLAKE3_OUT_LEN];

    job->hex[0] = '\0';
    if (g_fs == NULL || job->path == NULL) {
        job->status = -1;
        return;
    }
    if (!fx_fscap_path_allowed(g_fs, job->path)) {
        job->status = -5;
        return;
    }
    raw = read_all(job->path, &n);
    if (raw == NULL) {
        job->status = -6;
        return;
    }
    blake3_hasher_init(&h);
    blake3_hasher_update(&h, raw, n);
    blake3_hasher_finalize(&h, out, BLAKE3_OUT_LEN);
    free(raw);
    to_hex(out, BLAKE3_OUT_LEN, job->hex);
    job->status = 0;
}

static void worker(void *arg) {
    (void)arg;
    for (;;) {
        int i;
#ifdef _WIN32
        i = (int)InterlockedIncrement(&g_next) - 1;
#else
        i = atomic_fetch_add_explicit(&g_next, 1, memory_order_relaxed);
#endif
        if (i >= g_job_n) {
            return;
        }
        hash_job(&g_jobs[i]);
    }
}

int32_t fx_pipe_reset(void) {
    int i;
    for (i = 0; i < g_job_n; i++) {
        free(g_jobs[i].path);
        g_jobs[i].path = NULL;
        g_jobs[i].hex[0] = '\0';
        g_jobs[i].status = -1;
    }
    g_job_n = 0;
    g_fs = NULL;
    return 0;
}

int32_t fx_pipe_add_path(const char *path) {
    size_t n;
    char *copy;
    if (path == NULL || path[0] == '\0') {
        return FX_PIPE_USAGE;
    }
    if (g_job_n >= FX_PIPE_MAX_JOBS) {
        return FX_PIPE_USAGE;
    }
    n = strlen(path);
    copy = (char *)malloc(n + 1);
    if (copy == NULL) {
        return FX_PIPE_FAIL;
    }
    memcpy(copy, path, n + 1);
    g_jobs[g_job_n].path = copy;
    g_jobs[g_job_n].hex[0] = '\0';
    g_jobs[g_job_n].status = -1;
    g_job_n++;
    return FX_PIPE_OK;
}

int32_t fx_pipe_run(int64_t fs_handle, int32_t workers) {
    FxNursery *n;
    int w;
    int i;
    int any_fail = 0;
    int spawn_n;

    if (g_job_n <= 0) {
        return FX_PIPE_USAGE;
    }
    g_fs = fx_fscap_from_handle(fs_handle);
    if (g_fs == NULL) {
        return FX_PIPE_DENY;
    }
    if (workers < 1) {
        workers = 1;
    }
    if (workers > 16) {
        workers = 16;
    }
    if (workers > g_job_n) {
        workers = g_job_n;
    }

#ifdef _WIN32
    InterlockedExchange(&g_next, 0);
#else
    atomic_store(&g_next, 0);
#endif

    n = fx_nursery_create_sized(workers);
    if (n == NULL) {
        return FX_PIPE_FAIL;
    }
    spawn_n = workers;
    for (w = 0; w < spawn_n; w++) {
        if (fx_nursery_spawn(n, worker, NULL) != FX_NURSERY_OK) {
            fx_nursery_destroy(n);
            return FX_PIPE_FAIL;
        }
    }
    if (fx_nursery_join_all(n) != FX_NURSERY_OK) {
        fx_nursery_destroy(n);
        return FX_PIPE_FAIL;
    }
    fx_nursery_destroy(n);

    for (i = 0; i < g_job_n; i++) {
        if (g_jobs[i].status == -5) {
            return FX_PIPE_DENY;
        }
        if (g_jobs[i].status != 0) {
            any_fail = 1;
        }
    }
    return any_fail ? FX_PIPE_FAIL : FX_PIPE_OK;
}

int32_t fx_pipe_count(void) {
    return g_job_n;
}

const char *fx_pipe_path_at(int32_t i) {
    if (i < 0 || i >= g_job_n) {
        return "";
    }
    return g_jobs[i].path ? g_jobs[i].path : "";
}

const char *fx_pipe_hex_at(int32_t i) {
    if (i < 0 || i >= g_job_n) {
        return "";
    }
    return g_jobs[i].hex;
}

int32_t fx_pipe_status_at(int32_t i) {
    if (i < 0 || i >= g_job_n) {
        return -1;
    }
    return g_jobs[i].status;
}

/* Next non-empty stdin path line (trimmed). "" means EOF. Static buffer. */
const char *fx_pipe_stdin_next_path(void) {
    static char line[4096];
    for (;;) {
        size_t n;
        char *p;
        char *end;
        if (fgets(line, (int)sizeof(line), stdin) == NULL) {
            line[0] = '\0';
            return line;
        }
        n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[--n] = '\0';
        }
        p = line;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        end = p + strlen(p);
        while (end > p && (end[-1] == ' ' || end[-1] == '\t')) {
            end--;
            *end = '\0';
        }
        if (*p == '\0') {
            continue;
        }
        if (p != line) {
            memmove(line, p, (size_t)(end - p) + 1);
        }
        return line;
    }
}
