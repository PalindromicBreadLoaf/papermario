#include <string.h>
#include <PR/ultratypes.h>

// POSIX systems already have these in libc
#if defined(__GNUC__) || defined(__clang__)
#define WEAK __attribute__((weak))
#else
#define WEAK
#endif

WEAK void bcopy(const void *src, void *dst, size_t n) {
    memmove(dst, src, n);
}

WEAK int bcmp(const void *a, const void *b, size_t n) {
    return memcmp(a, b, (size_t)n);
}

WEAK void bzero(void *s, size_t n) {
    memset(s, 0, n);
}
