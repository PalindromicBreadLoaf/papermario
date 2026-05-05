#include <stdio.h>
#include "gbi_interpreter.h"
#include "rdp_state.h"
#include "gl_backend.h"

void gbi_init(void) {
    rdp_state_init();
}

void gbi_run_dl(Gfx *dl) {
    (void)dl;
}
