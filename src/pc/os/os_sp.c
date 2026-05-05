#include <PR/sptask.h>

void          osSpTaskStartGo(OSTask *task) { (void)task; }
void          osSpTaskYield(void)           {}
OSYieldResult osSpTaskYielded(OSTask *task) { (void)task; return 0; }
