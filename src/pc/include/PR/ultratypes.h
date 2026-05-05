#ifndef _ULTRATYPES_H_
#define _ULTRATYPES_H_

// PC-compatible replacement for include/PR/ultratypes.h.
// The N64 version uses `unsigned long` for u32, which is 8 bytes on x86-64.
// Here we use fixed-width types via <stdint.h> so the sizes match the N64.

#include <stddef.h>
#include <stdint.h>

typedef signed char        s8;
typedef unsigned char      u8;
typedef signed short       s16;
typedef unsigned short     u16;
typedef signed int         s32;
typedef unsigned int       u32;
typedef signed long long   s64;
typedef unsigned long long u64;

typedef volatile u8  vu8;
typedef volatile u16 vu16;
typedef volatile u32 vu32;
typedef volatile u64 vu64;
typedef volatile s8  vs8;
typedef volatile s16 vs16;
typedef volatile s32 vs32;
typedef volatile s64 vs64;

typedef float  f32;
typedef double f64;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NULL
#define NULL 0
#endif

#endif /* _ULTRATYPES_H_ */
