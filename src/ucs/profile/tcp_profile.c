/* Copyright (c) 2026. See UCX LICENSE for terms. */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "tcp_profile.h"

#if UCS_TCP_PROFILE_ENABLED
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

/* Bounded single-writer logs, allocated and faulted before capture. */
#define UCS_TP_THREADS 32
#define UCS_TP_CAPACITY 262144

typedef struct ucs_tcp_profile_record {
    uint64_t time, object, a, b;
    uint32_t event, kind;
} ucs_tcp_profile_record_t;

typedef struct ucs_tcp_profile_thread {
    uint64_t tid, count, dropped, last_time;
    ucs_tcp_profile_record_t *records;
    uint64_t calls[UCS_TP_NUM_EVENTS];
    uint64_t elapsed[UCS_TP_NUM_EVENTS];
} __attribute__((aligned(64))) ucs_tcp_profile_thread_t;

static ucs_tcp_profile_thread_t ucs_tp_threads[UCS_TP_THREADS];
static ucs_tcp_profile_record_t *ucs_tp_storage;
static atomic_uint ucs_tp_next_thread;
static atomic_uint ucs_tp_unregistered;
static atomic_int ucs_tp_active;
static _Thread_local ucs_tcp_profile_thread_t *ucs_tp_thread;
static _Thread_local int ucs_tp_thread_failed;

/* Empty progress/status polls can outnumber data events by orders of magnitude.
 * Retain exact inclusive duration/count for these scopes, without emitting a
 * begin/end pair on every poll. This policy only exists in ON builds. */
static int ucs_tcp_profile_is_counter(unsigned event)
{
    switch (event) {
    case UCS_TP_NIXL_STATUS:
    case UCS_TP_NIXL_NOTIFS:
    case UCS_TP_NIXL_LOCK_WAIT:
    case UCS_TP_BACKEND_CHECK:
    case UCS_TP_BACKEND_STATUS:
    case UCS_TP_NOTIF_RETRIEVE:
    case UCS_TP_WORKER_PROGRESS:
    case UCS_TP_UCP_PROGRESS:
    case UCS_TP_TCP_PROGRESS:
        return 1;
    default:
        return 0;
    }
}

int ucs_tcp_profile_init(void)
{
    volatile unsigned char *p;
    const size_t size = UCS_TP_THREADS * UCS_TP_CAPACITY *
                        sizeof(*ucs_tp_storage);

    if (ucs_tp_storage != NULL) {
        return -1; /* No unsafe reset while other threads may be recording. */
    }

    ucs_tp_storage = calloc(UCS_TP_THREADS * UCS_TP_CAPACITY,
                            sizeof(*ucs_tp_storage));
    if (ucs_tp_storage == NULL) {
        return -1;
    }

    /* calloc alone is lazy: force private physical pages before timing. */
    p = (volatile unsigned char *)ucs_tp_storage;
    for (size_t i = 0; i < size; i += 4096) {
        p[i] = 0;
    }
    for (unsigned i = 0; i < UCS_TP_THREADS; ++i) {
        ucs_tp_threads[i].records = ucs_tp_storage + i * UCS_TP_CAPACITY;
    }
    atomic_store_explicit(&ucs_tp_active, 1, memory_order_release);
    return 0;
}

uint64_t ucs_tcp_profile_record(unsigned event, unsigned kind,
                               uintptr_t object, uint64_t a, uint64_t b)
{
    struct timespec ts;
    ucs_tcp_profile_thread_t *thread;
    ucs_tcp_profile_record_t *record;
    uint64_t now;
    const int saved_errno = errno;
    unsigned index;

    if (!atomic_load_explicit(&ucs_tp_active, memory_order_acquire)) {
        return 0;
    }

    thread = ucs_tp_thread;
    if (thread == NULL) {
        if (ucs_tp_thread_failed) {
            return 0;
        }
        index = atomic_fetch_add_explicit(&ucs_tp_next_thread, 1,
                                          memory_order_relaxed);
        if (index >= UCS_TP_THREADS) {
            ucs_tp_thread_failed = 1;
            atomic_fetch_add_explicit(&ucs_tp_unregistered, 1,
                                       memory_order_relaxed);
            return 0;
        }
        thread = &ucs_tp_threads[index];
        thread->tid = (uint64_t)syscall(SYS_gettid);
        ucs_tp_thread = thread;
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    now = (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
    errno = saved_errno;
    thread->last_time = now;
    if (ucs_tcp_profile_is_counter(event)) {
        if (kind == 'E') {
            ++thread->calls[event];
            thread->elapsed[event] += now - b;
        }
        return now;
    }
    if (thread->count == UCS_TP_CAPACITY) {
        ++thread->dropped;
        return now;
    }

    record         = &thread->records[thread->count++];
    record->time   = now;
    record->object = object;
    record->a      = a;
    record->b      = b;
    record->event  = event;
    record->kind   = kind;
    return now;
}

void ucs_tcp_profile_stop(void)
{
    atomic_store_explicit(&ucs_tp_active, 0, memory_order_release);
}

int ucs_tcp_profile_dump(const char *path)
{
    FILE *file;
    unsigned count;
    int failed;
    /* Little-endian native records; converter validates sizes and byte order. */
    const unsigned char magic[8] = {'U', 'C', 'S', 'T', 'P', '0', '1', 0};
    uint64_t header[5];

    if ((ucs_tp_storage == NULL) || atomic_load(&ucs_tp_active)) {
        return -1;
    }
    count = atomic_load(&ucs_tp_next_thread);
    if (count > UCS_TP_THREADS) {
        count = UCS_TP_THREADS;
    }
    header[0] = 2; /* v2 adds counter records, kind 'C', emitted only at dump */
    header[1] = sizeof(ucs_tcp_profile_record_t);
    header[2] = getpid();
    header[3] = count;
    header[4] = atomic_load(&ucs_tp_unregistered);
    file = fopen(path, "wb");
    if (file == NULL) {
        return -1;
    }
    failed = (fwrite(magic, sizeof(magic), 1, file) != 1) ||
             (fwrite(header, sizeof(header), 1, file) != 1);
    for (unsigned i = 0; (i < count) && !failed; ++i) {
        ucs_tcp_profile_thread_t *thread = &ucs_tp_threads[i];
        uint64_t summaries = 0;
        uint64_t th[3];

        for (unsigned event = 1; event < UCS_TP_NUM_EVENTS; ++event) {
            summaries += (thread->calls[event] != 0);
        }
        th[0] = thread->tid;
        th[1] = thread->count + summaries;
        th[2] = thread->dropped;
        failed = (fwrite(th, sizeof(th), 1, file) != 1) ||
                 (fwrite(thread->records, sizeof(*thread->records),
                         thread->count, file) != thread->count);
        for (unsigned event = 1; !failed && (event < UCS_TP_NUM_EVENTS); ++event) {
            ucs_tcp_profile_record_t summary;

            if (thread->calls[event] == 0) {
                continue;
            }
            summary.time   = thread->last_time;
            summary.object = 0;
            summary.a      = thread->calls[event];
            summary.b      = thread->elapsed[event];
            summary.event  = event;
            summary.kind   = 'C';
            failed = (fwrite(&summary, sizeof(summary), 1, file) != 1);
        }
    }
    if (fclose(file) != 0) {
        failed = 1;
    }
    return failed ? -1 : 0;
}
#endif
