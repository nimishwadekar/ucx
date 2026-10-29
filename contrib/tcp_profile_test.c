/* Standalone tests: no CUDA, networking, or UCX initialization required. */
#include "tcp_profile.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void *worker(void *arg)
{
    (void)arg;
    for (unsigned i = 0; i < 1000; ++i) {
        UCS_TP_SCOPE(UCS_TP_BENCH_POST, arg, i);
        UCS_TP_MARK(UCS_TP_GET_ID, arg, i, 0);
        {
            UCS_TP_SCOPE(UCS_TP_NIXL_STATUS, arg, i);
        }
    }
    return NULL;
}

int main(int argc, char **argv)
{
#if UCS_TCP_PROFILE_ENABLED
    pthread_t threads[4];
    assert(ucs_tcp_profile_init() == 0);
    assert(ucs_tcp_profile_dump("must-not-be-created.bin") == -1);
    errno = EAGAIN;
    UCS_TP_MARK(UCS_TP_SETUP, 0, 0, 0);
    assert(errno == EAGAIN);
    for (uintptr_t i = 0; i < 4; ++i) {
        assert(pthread_create(&threads[i], NULL, worker, (void *)i) == 0);
    }
    for (unsigned i = 0; i < 4; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
    }
    if (argc > 2) {
        for (unsigned i = 0; i < 300000; ++i) {
            UCS_TP_MARK(UCS_TP_SETUP, 0, i, 0);
        }
    }
    ucs_tcp_profile_stop();
    assert(ucs_tcp_profile_dump(argv[1]) == 0);
#else
    int side_effect = 0;
    UCS_TP_SCOPE(UNDEFINED_EVENT, ++side_effect, ++side_effect);
    UCS_TP_MARK(UNDEFINED_EVENT, ++side_effect, ++side_effect, ++side_effect);
    assert(side_effect == 0);
    (void)argc;
    (void)argv;
    (void)worker;
#endif
    puts("recorder test passed");
    return 0;
}
