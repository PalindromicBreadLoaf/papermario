#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <SDL2/SDL.h>
#include "glad/gl.h"
#include "gl_backend.h"
#include "rdp_state.h"

// GLSL sources
static const char *s_vert_src =
    "#version 330 core\n"
    "layout(location = 0) in vec4 a_pos;\n"
    "layout(location = 1) in vec2 a_uv0;\n"
    "layout(location = 2) in vec2 a_uv1;\n"
    "layout(location = 3) in vec4 a_color;\n"
    "out vec2 v_uv0;\n"
    "out vec2 v_uv1;\n"
    "out vec4 v_color;\n"
    "void main() {\n"
    "    v_uv0       = a_uv0;\n"
    "    v_uv1       = a_uv1;\n"
    "    v_color     = a_color;\n"
    "    gl_Position = a_pos;\n"
    "}\n";

static const char *s_frag_src =
    "#version 330 core\n"
    "in vec2 v_uv0;\n"
    "in vec2 v_uv1;\n"
    "in vec4 v_color;\n"
    "uniform sampler2D u_tex0;\n"
    "uniform sampler2D u_tex1;\n"
    "uniform vec4 u_prim;\n"
    "uniform vec4 u_env;\n"
    "uniform vec4 u_fog_color;\n"
    "uniform int  u_use_tex;\n"
    "uniform int  u_use_fog;\n"
    "uniform int  u_alpha_test;\n"
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
    "    vec4 tex0 = ((u_use_tex & 1) != 0) ? texture(u_tex0, v_uv0) : vec4(1.0);\n"
    "    vec4 tex1 = ((u_use_tex & 2) != 0) ? texture(u_tex1, v_uv1) : vec4(1.0);\n"
    "    vec3 rgb_src[12] = vec3[12](\n"
    "        tex0.rgb, tex1.rgb, v_color.rgb, u_prim.rgb, u_env.rgb,\n"
    "        vec3(0.0), vec3(1.0),\n"
    "        vec3(tex0.a), vec3(tex1.a), vec3(v_color.a), vec3(u_prim.a), vec3(u_env.a));\n"
    "    float a_src[7] = float[7](\n"
    "        tex0.a, tex1.a, v_color.a, u_prim.a, u_env.a, 0.0, 1.0);\n"
    "    vec3  cc_a = rgb_src[clamp(u_cc_rgb_a, 0, 11)];\n"
    "    vec3  cc_b = rgb_src[clamp(u_cc_rgb_b, 0, 11)];\n"
    "    vec3  cc_c = rgb_src[clamp(u_cc_rgb_c, 0, 11)];\n"
    "    vec3  cc_d = rgb_src[clamp(u_cc_rgb_d, 0, 11)];\n"
    "    float ca_a = a_src[clamp(u_cc_a_a, 0, 6)];\n"
    "    float ca_b = a_src[clamp(u_cc_a_b, 0, 6)];\n"
    "    float ca_c = a_src[clamp(u_cc_a_c, 0, 6)];\n"
    "    float ca_d = a_src[clamp(u_cc_a_d, 0, 6)];\n"
    "    vec4 color;\n"
    "    color.rgb = (cc_a - cc_b) * cc_c + cc_d;\n"
    "    color.a   = (ca_a - ca_b) * ca_c + ca_d;\n"
    "    if (u_use_fog != 0)\n"
    "        color.rgb = mix(color.rgb, u_fog_color.rgb, v_color.a);\n"
    "    if (u_alpha_test == 1 && color.a <= 0.0)\n"
    "        discard;\n"
    "    if (u_alpha_test == 2) {\n"
    "        if (color.a < 0.5)\n"
    "            discard;\n"
    "        color.a = 1.0;\n"
    "    }\n"
    "    frag_color = color;\n"
    "}\n";

typedef struct {
    GLint tex0, tex1;
    GLint prim, env, fog_color;
    GLint use_tex, use_fog, alpha_test;
    GLint cc_rgb_a, cc_rgb_b, cc_rgb_c, cc_rgb_d;
    GLint cc_a_a,  cc_a_b,  cc_a_c,  cc_a_d;
} UniformLocs;

// Module state
static SDL_Window    *s_window;
static SDL_GLContext  s_gl_ctx;
static GLuint         s_vao;
static GLuint         s_vbo;
static GLuint         s_program;
static GLuint         s_prev_frame_tex;
static GLuint         s_black_tex;
static int            s_prev_frame_w;
static int            s_prev_frame_h;
static bool           s_prev_frame_valid;
static UniformLocs    s_uloc;

float  gfx_buf_vbo[GFX_MAX_BUFFERED * 3 * GFX_FLOATS_PER_VTX];
size_t gfx_buf_vbo_len      = 0;
size_t gfx_buf_vbo_num_tris = 0;
int    gfx_use_tex          = 0;
int    gfx_alpha_test       = 0;
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
    extern bool gfx_trace_state;
    {
        const char *env = getenv("PM_TRACE_STATE");
        if (env && *env && *env != '0') gfx_trace_state = true;
    }
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

    // Fixed layout: vec4 pos (loc 0) | vec2 uv0 (loc 1) | vec2 uv1 (loc 2) | vec4 color (loc 3)
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
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void *)(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void *)(8 * sizeof(float)));

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
    s_uloc.alpha_test = glGetUniformLocation(s_program, "u_alpha_test");
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

static void gl_backend_dump_screenshot(int frame_index) {
    char path[64];
    snprintf(path, sizeof(path), "/tmp/pm_frame_%05d.rgba", frame_index);
    int w = gl_window_width;
    int h = gl_window_height;
    if (w <= 0 || h <= 0) return;
    size_t bytes = (size_t)w * (size_t)h * 4u;
    unsigned char *buf = (unsigned char *)malloc(bytes);
    if (!buf) return;
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(&w, sizeof(w), 1, f);
        fwrite(&h, sizeof(h), 1, f);
        fwrite(buf, 1, bytes, f);
        fclose(f);
    }
    free(buf);
}

static GLuint gl_backend_black_texture(void) {
    static const unsigned char black[] = { 0, 0, 0, 255 };
    GLint active_texture = GL_TEXTURE0;
    GLint bound_texture = 0;

    if (s_black_tex != 0) {
        return s_black_tex;
    }

    glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound_texture);

    glGenTextures(1, &s_black_tex);
    glBindTexture(GL_TEXTURE_2D, s_black_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, black);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glBindTexture(GL_TEXTURE_2D, (GLuint)bound_texture);
    glActiveTexture((GLenum)active_texture);
    return s_black_tex;
}

static void gl_backend_capture_previous_frame(void) {
    if (gl_window_width <= 0 || gl_window_height <= 0) {
        s_prev_frame_valid = false;
        return;
    }

    GLint active_texture = GL_TEXTURE0;
    GLint bound_texture = 0;

    glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound_texture);

    if (s_prev_frame_tex == 0) {
        glGenTextures(1, &s_prev_frame_tex);
    }

    glBindTexture(GL_TEXTURE_2D, s_prev_frame_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    if (s_prev_frame_w != gl_window_width || s_prev_frame_h != gl_window_height) {
        s_prev_frame_w = gl_window_width;
        s_prev_frame_h = gl_window_height;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, s_prev_frame_w, s_prev_frame_h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }

    glReadBuffer(GL_BACK);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, s_prev_frame_w, s_prev_frame_h);
    s_prev_frame_valid = true;

    glBindTexture(GL_TEXTURE_2D, (GLuint)bound_texture);
    glActiveTexture((GLenum)active_texture);
}

void gl_backend_end_frame(void) {
    gfx_flush();
    static int frame_counter = 0;
    const char *env = getenv("PM_DUMP_FRAMES");
    if (env && *env) {
        int interval = atoi(env);
        if (interval > 0 && (frame_counter % interval) == 0) {
            gl_backend_dump_screenshot(frame_counter);
        }
    }
    frame_counter++;
    gl_backend_capture_previous_frame();
    SDL_GL_SwapWindow(s_window);
}

unsigned int gl_backend_previous_frame_texture(void) {
    return s_prev_frame_valid ? s_prev_frame_tex : gl_backend_black_texture();
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
    glUniform1i(s_uloc.alpha_test, gfx_alpha_test);

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
    if (s_prev_frame_tex) { glDeleteTextures(1, &s_prev_frame_tex); s_prev_frame_tex = 0; }
    if (s_black_tex) { glDeleteTextures(1, &s_black_tex); s_black_tex = 0; }
    if (s_program) { glDeleteProgram(s_program);         s_program = 0; }
    if (s_vbo)     { glDeleteBuffers(1, &s_vbo);         s_vbo     = 0; }
    if (s_vao)     { glDeleteVertexArrays(1, &s_vao);    s_vao     = 0; }
    if (s_gl_ctx)  { SDL_GL_DeleteContext(s_gl_ctx);     s_gl_ctx  = NULL; }
    if (s_window)  { SDL_DestroyWindow(s_window);        s_window  = NULL; }
    SDL_Quit();
}
