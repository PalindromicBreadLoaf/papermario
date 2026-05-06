#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include "glad/gl.h"
#include "gl_backend.h"

// VBO layout must match: vec4 pos (loc 0) | vec2 uv (loc 1) | vec4 color (loc 2)

static const char *s_vert_src =
    "#version 330 core\n"
    "layout(location = 0) in vec4 a_pos;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "layout(location = 2) in vec4 a_color;\n"
    "out vec2 v_uv;\n"
    "out vec4 v_color;\n"
    "void main() {\n"
    "    v_uv      = a_uv;\n"
    "    v_color   = a_color;\n"
    "    gl_Position = a_pos;\n"
    "}\n";

static const char *s_frag_src =
    "#version 330 core\n"
    "in  vec2 v_uv;\n"
    "in  vec4 v_color;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    frag_color = v_color;\n"
    "}\n";

static SDL_Window    *s_window;
static SDL_GLContext  s_gl_ctx;
static GLuint         s_vao;
static GLuint         s_vbo;
static GLuint         s_program;


float  gfx_buf_vbo[GFX_MAX_BUFFERED * 3 * GFX_FLOATS_PER_VTX];
size_t gfx_buf_vbo_len      = 0;
size_t gfx_buf_vbo_num_tris = 0;


static void GLAPIENTRY gl_debug_callback(GLenum source, GLenum type, GLuint id,
                                          GLenum severity, GLsizei length,
                                          const GLchar *message,
                                          const void *userParam) {
    (void)source; (void)id; (void)length; (void)userParam;
    // Suppress low-severity notifications to keep the log useful.
    if (severity == 0x9147u /* GL_DEBUG_SEVERITY_NOTIFICATION */) return;
    fprintf(stderr, "GL[type=0x%04X sev=0x%04X]: %s\n", type, severity, message);
}

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        char log[1024];
        glGetShaderInfoLog(shader, (GLsizei)sizeof(log), NULL, log);
        fprintf(stderr, "Shader compilation failed (type 0x%X):\n%s\n", type, log);
        abort();
    }
    return shader;
}

// Public API

void gl_backend_init(const char *title, int width, int height) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        abort();
    }

    // Request OpenGL 3.3 core profile before window creation.
    // macOS requires the PROFILE_MASK attribute or it defaults to a legacy 2.1 context.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    s_window = SDL_CreateWindow(title,
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                width, height,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!s_window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        abort();
    }

    s_gl_ctx = SDL_GL_CreateContext(s_window);
    if (!s_gl_ctx) {
        fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
        abort();
    }

    SDL_GL_SetSwapInterval(1);  // vsync on

    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "gladLoadGL: failed to load one or more required OpenGL symbols\n");
        abort();
    }

    if (glDebugMessageCallback) {
        glEnable(GL_DEBUG_OUTPUT);
        glDebugMessageCallback(gl_debug_callback, NULL);
    }

    // One persistent VAO with a single dynamic VBO.  The vertex attrib layout
    // is fixed to match the VBO packed by gfx_sp_tri1:
    //   floats 0-3 : clip-space position  (vec4)
    //   floats 4-5 : texture coordinates  (vec2)
    //   floats 6-9 : RGBA colour          (vec4)
    glGenVertexArrays(1, &s_vao);
    glBindVertexArray(s_vao);

    glGenBuffers(1, &s_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);

    const GLsizei stride = (GLsizei)(GFX_FLOATS_PER_VTX * sizeof(float));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, stride, (void *)(0 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void *)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void *)(6 * sizeof(float)));

    GLuint vs = compile_shader(GL_VERTEX_SHADER,   s_vert_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, s_frag_src);

    s_program = glCreateProgram();
    glAttachShader(s_program, vs);
    glAttachShader(s_program, fs);
    glLinkProgram(s_program);

    GLint ok = GL_FALSE;
    glGetProgramiv(s_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(s_program, (GLsizei)sizeof(log), NULL, log);
        fprintf(stderr, "Shader link failed:\n%s\n", log);
        abort();
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    glUseProgram(s_program);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_SCISSOR_TEST);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-2.0f, -2.0f);
}

void gl_backend_start_frame(void) {
    // Disable scissor for the clear so the whole framebuffer is wiped.
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);   // depth writes must be on to clear the Z-buffer
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
}

void gl_backend_end_frame(void) {
    gfx_flush();
    SDL_GL_SwapWindow(s_window);
}

void gfx_flush(void) {
    if (gfx_buf_vbo_num_tris == 0) return;

    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(gfx_buf_vbo_len * sizeof(float)),
                 gfx_buf_vbo,
                 GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(gfx_buf_vbo_num_tris * 3));

    gfx_buf_vbo_len      = 0;
    gfx_buf_vbo_num_tris = 0;
}

void gl_backend_shutdown(void) {
    if (s_program) { glDeleteProgram(s_program); s_program = 0; }
    if (s_vbo)     { glDeleteBuffers(1, &s_vbo);          s_vbo = 0; }
    if (s_vao)     { glDeleteVertexArrays(1, &s_vao);     s_vao = 0; }
    if (s_gl_ctx)  { SDL_GL_DeleteContext(s_gl_ctx);  s_gl_ctx = NULL; }
    if (s_window)  { SDL_DestroyWindow(s_window);      s_window = NULL; }
    SDL_Quit();
}
