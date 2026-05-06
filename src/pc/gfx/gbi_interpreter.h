#ifndef GBI_INTERPRETER_H
#define GBI_INTERPRETER_H

#include <stdbool.h>
#include <PR/gbi.h>

// Initialise the interpreter (resets RSP/RDP state to defaults).
void gbi_init(void);

// Execute a display list. May recurse up to GFX_DL_STACK_DEPTH levels deep
// via gSPDisplayList calls.
void gbi_run_dl(Gfx *dl);

// When true, gbi_run_dl prints each decoded command to stderr.
extern bool gfx_dump_dl;

#endif /* GBI_INTERPRETER_H */
