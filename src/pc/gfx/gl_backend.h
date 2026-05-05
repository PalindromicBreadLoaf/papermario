#ifndef GL_BACKEND_H
#define GL_BACKEND_H

// Create the SDL2 window and OpenGL 3.3 core context.
void gl_backend_init(const char *title, int width, int height);

// Clear the framebuffer. Call at the start of each game frame.
void gl_backend_start_frame(void);

// Swap buffers. Call at the end of each game frame.
void gl_backend_end_frame(void);

// Upload the pending vertex batch and issue a draw call, then reset the batch.
void gfx_flush(void);

// Shut down SDL2 and destroy the GL context.
void gl_backend_shutdown(void);

#endif /* GL_BACKEND_H */
