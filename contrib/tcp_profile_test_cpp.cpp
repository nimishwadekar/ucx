#include "tcp_profile.h"
#include <cassert>

static int early_return()
{
    UCS_TP_SCOPE(UCS_TP_SETUP, 0, 0);
    return 7;
}

int main(int argc, char **argv)
{
    (void)argc;
    assert(ucs_tcp_profile_init() == 0);
    assert(early_return() == 7);
    try {
        UCS_TP_SCOPE(UCS_TP_BENCH_CASE, 0, 0);
        throw 7;
    } catch (int value) {
        assert(value == 7);
    }
    ucs_tcp_profile_stop();
    assert(ucs_tcp_profile_dump(argv[1]) == 0);
}
