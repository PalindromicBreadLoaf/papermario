#ifndef GL_BACKEND_H
#define GL_BACKEND_H

#include <stddef.h>

// VBO layout: 10 floats per vertex: x,y,z,w | u,v | r,g,b,a
#define GFX_MAX_BUFFERED   256
#define GFX_FLOATS_PER_VTX  10

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

// Number of consecutively-bound texture units (0, 1, or 2); read by gfx_flush.
extern int gfx_use_tex;

// Shut down SDL2 and destroy the GL context.
void gl_backend_shutdown(void);

// Current window dimensions in pixels, updated every frame.
extern int gl_window_width;
extern int gl_window_height;

#endif /* GL_BACKEND_H */
