#include <stdio.h>
#include "gl_backend.h"

float  gfx_buf_vbo[GFX_MAX_BUFFERED * 3 * GFX_FLOATS_PER_VTX];
size_t gfx_buf_vbo_len = 0;
size_t gfx_buf_vbo_num_tris = 0;

void gl_backend_init(const char *title, int width, int height) {
    (void)title; (void)width; (void)height;
}

void gl_backend_start_frame(void) {
}

void gl_backend_end_frame(void) {
    gfx_flush();
}

void gfx_flush(void) {
    gfx_buf_vbo_len = 0;
    gfx_buf_vbo_num_tris = 0;
}

void gl_backend_shutdown(void) {
}
