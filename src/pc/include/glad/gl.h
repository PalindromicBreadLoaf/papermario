#ifndef GLAD_GL_H
#define GLAD_GL_H

// All GL entry points are loaded through gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress).
// Returns 1 on success, 0 if any required symbol could not be resolved.
//
// Do not include system <GL/gl.h> or <SDL2/SDL_opengl.h> alongside this header.
// GLAD owns all GL type and function pointer declarations.

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#  define GLAPIENTRY __stdcall
#else
#  define GLAPIENTRY
#endif

typedef unsigned int   GLenum;
typedef unsigned char  GLboolean;
typedef unsigned int   GLbitfield;
typedef int            GLint;
typedef int            GLsizei;
typedef unsigned int   GLuint;
typedef float          GLfloat;
typedef float          GLclampf;
typedef double         GLdouble;
typedef signed char    GLbyte;
typedef short          GLshort;
typedef unsigned char  GLubyte;
typedef unsigned short GLushort;
typedef ptrdiff_t      GLsizeiptr;
typedef ptrdiff_t      GLintptr;
typedef char           GLchar;
typedef uint64_t       GLuint64;
typedef int64_t        GLint64;
typedef void           GLvoid;

// Boolean
#define GL_FALSE                          0u
#define GL_TRUE                           1u

// Clear bits
#define GL_DEPTH_BUFFER_BIT               0x00000100u
#define GL_COLOR_BUFFER_BIT               0x00004000u

// Primitive types
#define GL_TRIANGLES                      0x0004u

// Depth comparison
#define GL_LESS                           0x0201u
#define GL_LEQUAL                         0x0203u

// Blend factors
#define GL_ZERO                           0u
#define GL_ONE                            1u
#define GL_SRC_COLOR                      0x0300u
#define GL_ONE_MINUS_SRC_COLOR            0x0301u
#define GL_SRC_ALPHA                      0x0302u
#define GL_ONE_MINUS_SRC_ALPHA            0x0303u
#define GL_DST_ALPHA                      0x0304u
#define GL_ONE_MINUS_DST_ALPHA            0x0305u
#define GL_DST_COLOR                      0x0306u
#define GL_ONE_MINUS_DST_COLOR            0x0307u

// Enable/Disable caps
#define GL_CULL_FACE                      0x0B44u
#define GL_DEPTH_TEST                     0x0B71u
#define GL_BLEND                          0x0BE2u
#define GL_SCISSOR_TEST                   0x0C11u
#define GL_POLYGON_OFFSET_FILL            0x8037u

// Data types
#define GL_UNSIGNED_BYTE                  0x1401u
#define GL_FLOAT                          0x1406u

// Texture targets and parameters
#define GL_TEXTURE_2D                     0x0DE1u
#define GL_NEAREST                        0x2600u
#define GL_LINEAR                         0x2601u
#define GL_TEXTURE_MAG_FILTER             0x2800u
#define GL_TEXTURE_MIN_FILTER             0x2801u
#define GL_TEXTURE_WRAP_S                 0x2802u
#define GL_TEXTURE_WRAP_T                 0x2803u
#define GL_REPEAT                         0x2901u
#define GL_CLAMP_TO_EDGE                  0x812Fu
#define GL_MIRRORED_REPEAT                0x8370u
#define GL_RGBA8                          0x8058u
#define GL_RGBA                           0x1908u
#define GL_TEXTURE0                       0x84C0u
#define GL_TEXTURE1                       0x84C1u

// Buffer objects
#define GL_ARRAY_BUFFER                   0x8892u
#define GL_STREAM_DRAW                    0x88E0u

// Shader / program
#define GL_FRAGMENT_SHADER                0x8B30u
#define GL_VERTEX_SHADER                  0x8B31u
#define GL_COMPILE_STATUS                 0x8B81u
#define GL_LINK_STATUS                    0x8B82u
#define GL_INFO_LOG_LENGTH                0x8B84u

// Debug output (GL 4.3 / KHR_debug - unavailable on macOS)
#define GL_DEBUG_OUTPUT                   0x92E0u
#define GL_DEBUG_OUTPUT_SYNCHRONOUS       0x8242u
#define GL_DEBUG_TYPE_ERROR               0x824Cu


typedef void (GLAPIENTRY *GLDEBUGPROC)(GLenum source,
                                        GLenum type,
                                        GLuint id,
                                        GLenum severity,
                                        GLsizei length,
                                        const GLchar *message,
                                        const void *userParam);

// Function pointer typedefs
// GL 1.0 / 1.1
typedef void     (GLAPIENTRY *PFNGLCLEARPROC)              (GLbitfield mask);
typedef void     (GLAPIENTRY *PFNGLCLEARCOLORPROC)         (GLclampf r, GLclampf g, GLclampf b, GLclampf a);
typedef void     (GLAPIENTRY *PFNGLENABLEPROC)             (GLenum cap);
typedef void     (GLAPIENTRY *PFNGLDISABLEPROC)            (GLenum cap);
typedef void     (GLAPIENTRY *PFNGLBLENDFUNCPROC)          (GLenum sfactor, GLenum dfactor);
typedef void     (GLAPIENTRY *PFNGLDEPTHMASKPROC)          (GLboolean flag);
typedef void     (GLAPIENTRY *PFNGLDEPTHFUNCPROC)          (GLenum func);
typedef void     (GLAPIENTRY *PFNGLVIEWPORTPROC)           (GLint x, GLint y, GLsizei w, GLsizei h);
typedef void     (GLAPIENTRY *PFNGLSCISSORPROC)            (GLint x, GLint y, GLsizei w, GLsizei h);
typedef void     (GLAPIENTRY *PFNGLPOLYGONOFFSETPROC)      (GLfloat factor, GLfloat units);
typedef void     (GLAPIENTRY *PFNGLDRAWARRAYSPROC)         (GLenum mode, GLint first, GLsizei count);
typedef void     (GLAPIENTRY *PFNGLGENTEXTURESPROC)        (GLsizei n, GLuint *textures);
typedef void     (GLAPIENTRY *PFNGLBINDTEXTUREPROC)        (GLenum target, GLuint texture);
typedef void     (GLAPIENTRY *PFNGLTEXIMAGE2DPROC)         (GLenum target, GLint level,
                                                            GLint internalFormat,
                                                            GLsizei width, GLsizei height,
                                                            GLint border, GLenum format,
                                                            GLenum type, const void *data);
typedef void     (GLAPIENTRY *PFNGLTEXPARAMETERIPROC)      (GLenum target, GLenum pname, GLint param);
typedef void     (GLAPIENTRY *PFNGLDELETETEXTURESPROC)     (GLsizei n, const GLuint *textures);

// GL 1.3
typedef void     (GLAPIENTRY *PFNGLACTIVETEXTUREPROC)      (GLenum texture);

// GL 1.5
typedef void     (GLAPIENTRY *PFNGLGENBUFFERSPROC)         (GLsizei n, GLuint *buffers);
typedef void     (GLAPIENTRY *PFNGLBINDBUFFERPROC)         (GLenum target, GLuint buffer);
typedef void     (GLAPIENTRY *PFNGLBUFFERDATAPROC)         (GLenum target, GLsizeiptr size,
                                                            const void *data, GLenum usage);
typedef void     (GLAPIENTRY *PFNGLDELETEBUFFERSPROC)      (GLsizei n, const GLuint *buffers);

// GL 2.0
typedef GLuint   (GLAPIENTRY *PFNGLCREATESHADERPROC)       (GLenum type);
typedef void     (GLAPIENTRY *PFNGLSHADERSOURCEPROC)       (GLuint shader, GLsizei count,
                                                            const GLchar *const *string,
                                                            const GLint *length);
typedef void     (GLAPIENTRY *PFNGLCOMPILESHADERPROC)      (GLuint shader);
typedef void     (GLAPIENTRY *PFNGLGETSHADERIVPROC)        (GLuint shader, GLenum pname, GLint *params);
typedef void     (GLAPIENTRY *PFNGLGETSHADERINFOLOGPROC)   (GLuint shader, GLsizei maxLen,
                                                            GLsizei *length, GLchar *infoLog);
typedef void     (GLAPIENTRY *PFNGLDELETESHADERPROC)       (GLuint shader);

typedef GLuint   (GLAPIENTRY *PFNGLCREATEPROGRAMPROC)      (void);
typedef void     (GLAPIENTRY *PFNGLATTACHSHADERPROC)       (GLuint program, GLuint shader);
typedef void     (GLAPIENTRY *PFNGLLINKPROGRAMPROC)        (GLuint program);
typedef void     (GLAPIENTRY *PFNGLGETPROGRAMIVPROC)       (GLuint program, GLenum pname, GLint *params);
typedef void     (GLAPIENTRY *PFNGLGETPROGRAMINFOLOGPROC)  (GLuint program, GLsizei maxLen,
                                                            GLsizei *length, GLchar *infoLog);
typedef void     (GLAPIENTRY *PFNGLUSEPROGRAMPROC)         (GLuint program);
typedef void     (GLAPIENTRY *PFNGLDELETEPROGRAMPROC)      (GLuint program);

// GL 2.0
typedef GLint    (GLAPIENTRY *PFNGLGETATTRIBLOCATIONPROC)         (GLuint program, const GLchar *name);
typedef void     (GLAPIENTRY *PFNGLENABLEVERTEXATTRIBARRAYPROC)   (GLuint index);
typedef void     (GLAPIENTRY *PFNGLDISABLEVERTEXATTRIBARRAYPROC)  (GLuint index);
typedef void     (GLAPIENTRY *PFNGLVERTEXATTRIBPOINTERPROC)       (GLuint index, GLint size,
                                                                    GLenum type, GLboolean normalized,
                                                                    GLsizei stride, const void *pointer);
typedef GLint    (GLAPIENTRY *PFNGLGETUNIFORMLOCATIONPROC)        (GLuint program, const GLchar *name);
typedef void     (GLAPIENTRY *PFNGLUNIFORM1IPROC)                 (GLint loc, GLint v0);
typedef void     (GLAPIENTRY *PFNGLUNIFORM4FPROC)                 (GLint loc, GLfloat v0, GLfloat v1,
                                                                    GLfloat v2, GLfloat v3);
typedef void     (GLAPIENTRY *PFNGLUNIFORM4FVPROC)                (GLint loc, GLsizei count,
                                                                    const GLfloat *value);

// GL 3.0
typedef void     (GLAPIENTRY *PFNGLGENVERTEXARRAYSPROC)    (GLsizei n, GLuint *arrays);
typedef void     (GLAPIENTRY *PFNGLBINDVERTEXARRAYPROC)    (GLuint array);
typedef void     (GLAPIENTRY *PFNGLDELETEVERTEXARRAYSPROC) (GLsizei n, const GLuint *arrays);

// GL 4.3
typedef void     (GLAPIENTRY *PFNGLDEBUGMESSAGECALLBACKPROC)(GLDEBUGPROC callback,
                                                              const void *userParam);

extern PFNGLCLEARPROC                     glClear;
extern PFNGLCLEARCOLORPROC                glClearColor;
extern PFNGLENABLEPROC                    glEnable;
extern PFNGLDISABLEPROC                   glDisable;
extern PFNGLBLENDFUNCPROC                 glBlendFunc;
extern PFNGLDEPTHMASKPROC                 glDepthMask;
extern PFNGLDEPTHFUNCPROC                 glDepthFunc;
extern PFNGLVIEWPORTPROC                  glViewport;
extern PFNGLSCISSORPROC                   glScissor;
extern PFNGLPOLYGONOFFSETPROC             glPolygonOffset;
extern PFNGLDRAWARRAYSPROC                glDrawArrays;
extern PFNGLGENTEXTURESPROC               glGenTextures;
extern PFNGLBINDTEXTUREPROC               glBindTexture;
extern PFNGLTEXIMAGE2DPROC                glTexImage2D;
extern PFNGLTEXPARAMETERIPROC             glTexParameteri;
extern PFNGLDELETETEXTURESPROC            glDeleteTextures;
extern PFNGLACTIVETEXTUREPROC             glActiveTexture;

extern PFNGLGENBUFFERSPROC                glGenBuffers;
extern PFNGLBINDBUFFERPROC                glBindBuffer;
extern PFNGLBUFFERDATAPROC                glBufferData;
extern PFNGLDELETEBUFFERSPROC             glDeleteBuffers;

extern PFNGLCREATESHADERPROC              glCreateShader;
extern PFNGLSHADERSOURCEPROC              glShaderSource;
extern PFNGLCOMPILESHADERPROC             glCompileShader;
extern PFNGLGETSHADERIVPROC               glGetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC          glGetShaderInfoLog;
extern PFNGLDELETESHADERPROC              glDeleteShader;

extern PFNGLCREATEPROGRAMPROC             glCreateProgram;
extern PFNGLATTACHSHADERPROC              glAttachShader;
extern PFNGLLINKPROGRAMPROC               glLinkProgram;
extern PFNGLGETPROGRAMIVPROC              glGetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC         glGetProgramInfoLog;
extern PFNGLUSEPROGRAMPROC                glUseProgram;
extern PFNGLDELETEPROGRAMPROC             glDeleteProgram;

extern PFNGLGETATTRIBLOCATIONPROC         glGetAttribLocation;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC   glEnableVertexAttribArray;
extern PFNGLDISABLEVERTEXATTRIBARRAYPROC  glDisableVertexAttribArray;
extern PFNGLVERTEXATTRIBPOINTERPROC       glVertexAttribPointer;

extern PFNGLGETUNIFORMLOCATIONPROC        glGetUniformLocation;
extern PFNGLUNIFORM1IPROC                 glUniform1i;
extern PFNGLUNIFORM4FPROC                 glUniform4f;
extern PFNGLUNIFORM4FVPROC                glUniform4fv;

extern PFNGLGENVERTEXARRAYSPROC           glGenVertexArrays;
extern PFNGLBINDVERTEXARRAYPROC           glBindVertexArray;
extern PFNGLDELETEVERTEXARRAYSPROC        glDeleteVertexArrays;

// NULL on platforms that do not expose GL 4.3
extern PFNGLDEBUGMESSAGECALLBACKPROC      glDebugMessageCallback;

// Loader

typedef void *(*GLADloadfunc)(const char *name);

// Load all GL function pointers via `load`.
// Returns 1 if all required symbols were resolved, 0 otherwise.
int gladLoadGL(GLADloadfunc load);

#endif /* GLAD_GL_H */
