#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include "glad/gl.h"
#include "gl_backend.h"
#include "rdp_state.h"

// GLSL sources (embedded to avoid runtime file-path dependencies).
static const char *s_vert_src =
    "#version 330 core\n"
    "layout(location = 0) in vec4 a_pos;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "layout(location = 2) in vec4 a_color;\n"
    "out vec2 v_uv;\n"
    "out vec4 v_color;\n"
    "void main() {\n"
    "    v_uv        = a_uv;\n"
    "    v_color     = a_color;\n"
    "    gl_Position = a_pos;\n"
    "}\n";

static const char *s_frag_src =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "in vec4 v_color;\n"
    "uniform sampler2D u_tex0;\n"
    "uniform sampler2D u_tex1;\n"
    "uniform vec4 u_prim;\n"
    "uniform vec4 u_env;\n"
    "uniform vec4 u_fog_color;\n"
    "uniform int  u_use_tex;\n"
    "uniform int  u_use_fog;\n"
    "uniform int  u_cc_rgb_a;\n"
    "uniform int  u_cc_rgb_b;\n"
    "uniform int  u_cc_rgb_c;\n"
    "uniform int  u_cc_rgb_d;\n"
    "uniform int  u_cc_a_a;\n"
    "uniform int  u_cc_a_b;\n"
    "uniform int  u_cc_a_c;\n"
    "uniform int  u_cc_a_d;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    vec4 tex0 = (u_use_tex >= 1) ? texture(u_tex0, v_uv) : vec4(1.0);\n"
    "    vec4 tex1 = (u_use_tex >= 2) ? texture(u_tex1, v_uv) : vec4(1.0);\n"
    "    vec3 rgb_src[7] = vec3[7](\n"
    "        tex0.rgb, tex1.rgb, v_color.rgb, u_prim.rgb, u_env.rgb,\n"
    "        vec3(0.0), vec3(1.0));\n"
    "    float a_src[7] = float[7](\n"
    "        tex0.a, tex1.a, v_color.a, u_prim.a, u_env.a, 0.0, 1.0);\n"
    "    vec3  cc_a = rgb_src[clamp(u_cc_rgb_a, 0, 6)];\n"
    "    vec3  cc_b = rgb_src[clamp(u_cc_rgb_b, 0, 6)];\n"
    "    vec3  cc_c = rgb_src[clamp(u_cc_rgb_c, 0, 6)];\n"
    "    vec3  cc_d = rgb_src[clamp(u_cc_rgb_d, 0, 6)];\n"
    "    float ca_a = a_src[clamp(u_cc_a_a, 0, 6)];\n"
    "    float ca_b = a_src[clamp(u_cc_a_b, 0, 6)];\n"
    "    float ca_c = a_src[clamp(u_cc_a_c, 0, 6)];\n"
    "    float ca_d = a_src[clamp(u_cc_a_d, 0, 6)];\n"
    "    vec4 color;\n"
    "    color.rgb = (cc_a - cc_b) * cc_c + cc_d;\n"
    "    color.a   = (ca_a - ca_b) * ca_c + ca_d;\n"
    "    if (u_use_fog != 0)\n"
    "        color.rgb = mix(color.rgb, u_fog_color.rgb, v_color.a);\n"
    "    frag_color = color;\n"
    "}\n";

typedef struct {
    GLint tex0, tex1;
    GLint prim, env, fog_color;
    GLint use_tex, use_fog;
    GLint cc_rgb_a, cc_rgb_b, cc_rgb_c, cc_rgb_d;
    GLint cc_a_a,  cc_a_b,  cc_a_c,  cc_a_d;
} UniformLocs;

// Module state
static SDL_Window    *s_window;
static SDL_GLContext  s_gl_ctx;
static GLuint         s_vao;
static GLuint         s_vbo;
static GLuint         s_program;
static UniformLocs    s_uloc;

float  gfx_buf_vbo[GFX_MAX_BUFFERED * 3 * GFX_FLOATS_PER_VTX];
size_t gfx_buf_vbo_len      = 0;
size_t gfx_buf_vbo_num_tris = 0;
int    gfx_use_tex          = 0;
int    gl_window_width      = 640;
int    gl_window_height     = 480;

// Helpers
static void GLAPIENTRY gl_debug_callback(GLenum source, GLenum type, GLuint id,
                                          GLenum severity, GLsizei length,
                                          const GLchar *message,
                                          const void *userParam) {
    (void)source; (void)id; (void)length; (void)userParam;
    if (severity == 0x826Bu) return; // GL_DEBUG_SEVERITY_NOTIFICATION
    fprintf(stderr, "GL[type=0x%04X sev=0x%04X]: %s\n", type, severity, message);
}

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
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

    // macOS requires PROFILE_MASK before window creation or it defaults to GL 2.1, which is bad
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

    SDL_GetWindowSize(s_window, &gl_window_width, &gl_window_height);
    SDL_GL_SetSwapInterval(1);

    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "gladLoadGL: failed to load required OpenGL symbols\n");
        abort();
    }

    if (glDebugMessageCallback) {
        glEnable(GL_DEBUG_OUTPUT);
        glDebugMessageCallback(gl_debug_callback, NULL);
    }

    // Fixed layout: vec4 pos (loc 0) | vec2 uv (loc 1) | vec4 color (loc 2)
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

    s_uloc.tex0      = glGetUniformLocation(s_program, "u_tex0");
    s_uloc.tex1      = glGetUniformLocation(s_program, "u_tex1");
    s_uloc.prim      = glGetUniformLocation(s_program, "u_prim");
    s_uloc.env       = glGetUniformLocation(s_program, "u_env");
    s_uloc.fog_color = glGetUniformLocation(s_program, "u_fog_color");
    s_uloc.use_tex   = glGetUniformLocation(s_program, "u_use_tex");
    s_uloc.use_fog   = glGetUniformLocation(s_program, "u_use_fog");
    s_uloc.cc_rgb_a  = glGetUniformLocation(s_program, "u_cc_rgb_a");
    s_uloc.cc_rgb_b  = glGetUniformLocation(s_program, "u_cc_rgb_b");
    s_uloc.cc_rgb_c  = glGetUniformLocation(s_program, "u_cc_rgb_c");
    s_uloc.cc_rgb_d  = glGetUniformLocation(s_program, "u_cc_rgb_d");
    s_uloc.cc_a_a    = glGetUniformLocation(s_program, "u_cc_a_a");
    s_uloc.cc_a_b    = glGetUniformLocation(s_program, "u_cc_a_b");
    s_uloc.cc_a_c    = glGetUniformLocation(s_program, "u_cc_a_c");
    s_uloc.cc_a_d    = glGetUniformLocation(s_program, "u_cc_a_d");

    glUniform1i(s_uloc.tex0, 0);
    glUniform1i(s_uloc.tex1, 1);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_SCISSOR_TEST);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-2.0f, -2.0f);

    SDL_GL_MakeCurrent(s_window, NULL);
}

void gl_backend_make_context_current(void) {
    if (SDL_GL_MakeCurrent(s_window, s_gl_ctx) != 0) {
        fprintf(stderr, "gl_backend: SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
    }
}

void gl_backend_start_frame(void) {
    SDL_GetWindowSize(s_window, &gl_window_width, &gl_window_height);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
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

    // Upload RDP colour registers (normalised to [0, 1]).
    glUniform4f(s_uloc.prim,
                g_rdp.prim_r / 255.0f, g_rdp.prim_g / 255.0f,
                g_rdp.prim_b / 255.0f, g_rdp.prim_a / 255.0f);
    glUniform4f(s_uloc.env,
                g_rdp.env_r / 255.0f,  g_rdp.env_g / 255.0f,
                g_rdp.env_b / 255.0f,  g_rdp.env_a / 255.0f);
    glUniform4f(s_uloc.fog_color,
                g_rdp.fog_r / 255.0f,  g_rdp.fog_g / 255.0f,
                g_rdp.fog_b / 255.0f,  g_rdp.fog_a / 255.0f);

    // Fog enable comes from the RSP geometry mode bit.
    glUniform1i(s_uloc.use_fog, (g_rsp.geometry_mode & G_FOG) ? 1 : 0);

    glUniform1i(s_uloc.use_tex, gfx_use_tex);

    glUniform1i(s_uloc.cc_rgb_a, g_rdp.cc_rgb_a);
    glUniform1i(s_uloc.cc_rgb_b, g_rdp.cc_rgb_b);
    glUniform1i(s_uloc.cc_rgb_c, g_rdp.cc_rgb_c);
    glUniform1i(s_uloc.cc_rgb_d, g_rdp.cc_rgb_d);
    glUniform1i(s_uloc.cc_a_a,   g_rdp.cc_a_a);
    glUniform1i(s_uloc.cc_a_b,   g_rdp.cc_a_b);
    glUniform1i(s_uloc.cc_a_c,   g_rdp.cc_a_c);
    glUniform1i(s_uloc.cc_a_d,   g_rdp.cc_a_d);

    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(gfx_buf_vbo_len * sizeof(float)),
                 gfx_buf_vbo,
                 GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(gfx_buf_vbo_num_tris * 3));

    gfx_buf_vbo_len      = 0;
    gfx_buf_vbo_num_tris = 0;
}

void gfx_bind_texture(int unit, unsigned int tex_id) {
    glActiveTexture(GL_TEXTURE0 + (GLenum)unit);
    glBindTexture(GL_TEXTURE_2D, (GLuint)tex_id);
}

void gl_backend_shutdown(void) {
    if (s_program) { glDeleteProgram(s_program);         s_program = 0; }
    if (s_vbo)     { glDeleteBuffers(1, &s_vbo);         s_vbo     = 0; }
    if (s_vao)     { glDeleteVertexArrays(1, &s_vao);    s_vao     = 0; }
    if (s_gl_ctx)  { SDL_GL_DeleteContext(s_gl_ctx);     s_gl_ctx  = NULL; }
    if (s_window)  { SDL_DestroyWindow(s_window);        s_window  = NULL; }
    SDL_Quit();
}
