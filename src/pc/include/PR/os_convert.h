#ifndef _OS_CONVERT_H_
#define	_OS_CONVERT_H_

#ifdef _LANGUAGE_C_PLUS_PLUS
extern "C" {
#endif

#include <PR/ultratypes.h>
#include <stdint.h>

#if defined(_LANGUAGE_C) || defined(_LANGUAGE_C_PLUS_PLUS)

#define	OS_CLOCK_RATE		62500000LL
#define	OS_CPU_COUNTER		(OS_CLOCK_RATE*3/4)

#define OS_NSEC_TO_CYCLES(n)	(((u64)(n)*(OS_CPU_COUNTER/15625000LL))/(1000000000LL/15625000LL))
#define OS_USEC_TO_CYCLES(n)	(((u64)(n)*(OS_CPU_COUNTER/15625LL))/(1000000LL/15625LL))
#define OS_CYCLES_TO_NSEC(c)	(((u64)(c)*(1000000000LL/15625000LL))/(OS_CPU_COUNTER/15625000LL))
#define OS_CYCLES_TO_USEC(c)	(((u64)(c)*(1000000LL/15625LL))/(OS_CPU_COUNTER/15625LL))

#define OS_K0_TO_PHYSICAL(x)    (uintptr_t)(x)
#define OS_K1_TO_PHYSICAL(x)    (uintptr_t)(x)
#define OS_PHYSICAL_TO_K0(x)    (void *)(uintptr_t)(x)
#define OS_PHYSICAL_TO_K1(x)    (void *)(uintptr_t)(x)

extern u32		 osVirtualToPhysical(void *);
extern void *		 osPhysicalToVirtual(u32);

#endif  /* defined(_LANGUAGE_C) || defined(_LANGUAGE_C_PLUS_PLUS) */

#ifdef _LANGUAGE_C_PLUS_PLUS
}
#endif

#endif /* !_OS_CONVERT_H_ */
