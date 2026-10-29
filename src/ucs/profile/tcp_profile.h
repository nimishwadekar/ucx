/* Copyright (c) 2026. See UCX LICENSE for terms. */
#ifndef UCS_TCP_PROFILE_H_
#define UCS_TCP_PROFILE_H_

#include <stdint.h>
#include <stddef.h>

#ifndef UCS_TCP_PROFILE_ENABLED
#define UCS_TCP_PROFILE_ENABLED 0
#endif

/* Diagnostic-only API, not a stable UCX API. No wire/object layout changes. */
enum ucs_tcp_profile_event {
    UCS_TP_BENCH_PREPARE = 1, UCS_TP_BENCH_POST, UCS_TP_BENCH_POLL,
    UCS_TP_BENCH_RECYCLE, UCS_TP_BENCH_CASE, UCS_TP_TARGET_POLL,
    UCS_TP_NIXL_CREATE, UCS_TP_NIXL_MAKE, UCS_TP_NIXL_POST,
    UCS_TP_NIXL_STATUS, UCS_TP_NIXL_NOTIFS, UCS_TP_NIXL_RELEASE,
    UCS_TP_BACKEND_PREP, UCS_TP_BACKEND_POST, UCS_TP_BACKEND_CHECK,
    UCS_TP_BACKEND_BATCH, UCS_TP_BACKEND_STATUS, UCS_TP_NOTIF_SEND,
    UCS_TP_NOTIF_RECEIVE, UCS_TP_NOTIF_RETRIEVE, UCS_TP_WORKER_PROGRESS,
    UCS_TP_UCP_GET, UCS_TP_GET_REQUEST_PROGRESS, UCS_TP_GET_REQUEST_RECEIVE,
    UCS_TP_GET_REPLY_PROGRESS, UCS_TP_GET_REPLY_PACK,
    UCS_TP_GET_REPLY_RECEIVE, UCS_TP_MEM_PACK, UCS_TP_MEM_UNPACK,
    UCS_TP_MEM_REG, UCS_TP_MEM_UNREG, UCS_TP_CUDA_D2H,
    UCS_TP_CUDA_H2D, UCS_TP_CUDA_SUBMIT, UCS_TP_CUDA_SYNC,
    UCS_TP_TCP_BCOPY, UCS_TP_TCP_SEND, UCS_TP_TCP_RECV,
    UCS_TP_TCP_SENDV, UCS_TP_TCP_RX_PARSE, UCS_TP_TCP_PROGRESS,
    UCS_TP_SOCKET_SEND, UCS_TP_SOCKET_RECV, UCS_TP_SOCKET_SENDV,
    UCS_TP_FLUSH, UCS_TP_UCP_PROGRESS, UCS_TP_GET_FRAGMENT,
    UCS_TP_GET_DATA_READY, UCS_TP_GET_ID, UCS_TP_SOCKET_RESULT,
    UCS_TP_BENCH_DONE, UCS_TP_UCP_COMPLETE, UCS_TP_NIXL_LOCK_WAIT,
    UCS_TP_UCP_LOCK_WAIT, UCS_TP_BARRIER, UCS_TP_SETUP,
    UCS_TP_POOL_QUEUE, UCS_TP_POOL_TASK, UCS_TP_POOL_WAIT,
    UCS_TP_UCP_SUBMIT, UCS_TP_GET_PAYLOAD_BYTES,
    UCS_TP_NUM_EVENTS
};

#define UCS_TP_JOIN_(_a, _b) _a##_b
#define UCS_TP_JOIN(_a, _b) UCS_TP_JOIN_(_a, _b)

#if UCS_TCP_PROFILE_ENABLED
#ifdef __cplusplus
extern "C" {
#endif

/* Initialize before creating any profiled threads. Returns 0 or -1. */
int ucs_tcp_profile_init(void);
/* Stop only after all recording threads have quiesced. */
void ucs_tcp_profile_stop(void);
/* Call after stop, outside all measured regions. Caller chooses output path. */
int ucs_tcp_profile_dump(const char *path);
uint64_t ucs_tcp_profile_record(unsigned event, unsigned kind,
                               uintptr_t object, uint64_t a, uint64_t b);

#ifdef __cplusplus
}
#endif

typedef struct ucs_tcp_profile_scope {
    uint64_t start;
    uintptr_t object;
    uint64_t a;
    unsigned event;
} ucs_tcp_profile_scope_t;

static inline ucs_tcp_profile_scope_t
ucs_tcp_profile_scope_begin(unsigned event, uintptr_t object, uint64_t a)
{
    ucs_tcp_profile_scope_t scope;
    scope.event  = event;
    scope.object = object;
    scope.a      = a;
    scope.start  = ucs_tcp_profile_record(event, 'B', object, a, 0);
    return scope;
}

static inline void ucs_tcp_profile_scope_end(ucs_tcp_profile_scope_t *scope)
{
    if (scope->start != 0) {
        ucs_tcp_profile_record(scope->event, 'E', scope->object,
                               scope->a, scope->start);
    }
}

#define UCS_TP_SCOPE(_event, _object, _a) \
    ucs_tcp_profile_scope_t UCS_TP_JOIN(ucs_tp_scope_, __LINE__) \
        __attribute__((cleanup(ucs_tcp_profile_scope_end))) = \
            ucs_tcp_profile_scope_begin((_event), (uintptr_t)(_object), \
                                         (uint64_t)(_a))
#define UCS_TP_MARK(_event, _object, _a, _b) \
    ((void)ucs_tcp_profile_record((_event), 'i', (uintptr_t)(_object), \
                                   (uint64_t)(_a), (uint64_t)(_b)))
#else
/* Arguments are not evaluated, including expressions with side effects. */
#define UCS_TP_SCOPE(_event, _object, _a) \
    typedef int UCS_TP_JOIN(ucs_tp_unused_scope_, __LINE__) \
        __attribute__((unused))
#define UCS_TP_MARK(_event, _object, _a, _b) do { } while (0)
#endif
#endif
