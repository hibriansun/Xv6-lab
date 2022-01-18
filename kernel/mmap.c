#include "types.h"
#include "fcntl.h"

// on success, it returns the address.
// on error, it returns 0xffffffffffffffff.
uint64
sys_mmap(void) {
    return 0xffffffffffffffff;
}

// on success, it returns 0.
// on error, it returns -1.
uint64
sys_munmap(void) {
    return -1;
}