#ifndef _OS_LIBC_H_
#define _OS_LIBC_H_

#include <stddef.h>
#include <stdio.h>

extern void bcopy(const void *, void *, size_t);
extern int  bcmp(const void *, const void *, size_t);
extern void bzero(void *, size_t);

extern int  sprintf(char *s, const char *fmt, ...);
extern void osSyncPrintf(const char *fmt, ...);

#endif
