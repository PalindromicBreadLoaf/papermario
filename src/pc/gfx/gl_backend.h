#ifndef GL_BACKEND_H
#define GL_BACKEND_H

#include <stddef.h>

// VBO layout: 12 floats per vertex: x,y,z,w | u0,v0 | u1,v1 | r,g,b,a
#define GFX_MAX_BUFFERED   256
#define GFX_FLOATS_PER_VTX  12

extern float  gfx_buf_vbo[GFX_MAX_BUFFERED * 3 * GFX_FLOATS_PER_VTX];
extern size_t gfx_buf_vbo_len;
extern size_t gfx_buf_vbo_num_tris;

// Create the SDL2 window and OpenGL 3.3 core context.
void gl_backend_init(const char *title, int width, int height);

// Clear the framebuffer. Call at the start of each game frame.
void gl_backend_start_frame(void);

// Swap buffers. Call at the end of each game frame.
void gl_backend_end_frame(void);

// Upload the pending vertex batch and issue a draw call, then reset the batch.
void gfx_flush(void);

// Bind a GL texture to the given unit (0 or 1).
void gfx_bind_texture(int unit, unsigned int tex_id);

// Bound texture-unit bitmask: bit 0 = TEXEL0, bit 1 = TEXEL1.
extern int gfx_use_tex;

// Non-zero discards fragments with alpha 0 before depth/blend writes.
// Use an int uniform; Mesa optimized out the previous float threshold.
extern int gfx_alpha_test;

// Shut down SDL2 and destroy the GL context.
void gl_backend_shutdown(void);

// Transfer the GL context to the calling thread. Call once from the dedicated
// render thread before any gl_backend_start_frame / gfx_flush / gl_backend_end_frame
// calls. The main thread must NOT call any GL functions after this point.
void gl_backend_make_context_current(void);

// Current window dimensions in pixels, updated every frame.
extern int gl_window_width;
extern int gl_window_height;

#endif /* GL_BACKEND_H */
