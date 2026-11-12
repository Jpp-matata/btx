// Thread-count shim for many-core test machines (testing only; dev hub 04-build-and-test.md section 3).
// test_btx sizes its thread pools from std::thread::hardware_concurrency() and get_nprocs(), which report
// every host CPU rather than the job's share. With LD_PRELOAD=libhwc.so they report BTX_HWC (default 1).
// Build: gcc -shared -fPIC -O2 -o libhwc.so contrib/ci-fork/hwc.c
#include <stdlib.h>
static int hwc_n(void) { const char* e = getenv("BTX_HWC"); int n = e ? atoi(e) : 0; return n > 0 ? n : 1; }
int get_nprocs(void) { return hwc_n(); }
int get_nprocs_conf(void) { return hwc_n(); }
unsigned int _ZNSt6thread20hardware_concurrencyEv(void) { return (unsigned int)hwc_n(); }
