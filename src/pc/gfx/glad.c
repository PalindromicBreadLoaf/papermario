#include <stdio.h>
#include "glad/gl.h"

PFNGLCLEARPROC                     glClear                    = NULL;
PFNGLCLEARCOLORPROC                glClearColor               = NULL;
PFNGLENABLEPROC                    glEnable                   = NULL;
PFNGLDISABLEPROC                   glDisable                  = NULL;
PFNGLBLENDFUNCPROC                 glBlendFunc                = NULL;
PFNGLDEPTHMASKPROC                 glDepthMask                = NULL;
PFNGLDEPTHFUNCPROC                 glDepthFunc                = NULL;
PFNGLVIEWPORTPROC                  glViewport                 = NULL;
PFNGLSCISSORPROC                   glScissor                  = NULL;
PFNGLPOLYGONOFFSETPROC             glPolygonOffset            = NULL;
PFNGLDRAWARRAYSPROC                glDrawArrays               = NULL;
PFNGLGENTEXTURESPROC               glGenTextures              = NULL;
PFNGLBINDTEXTUREPROC               glBindTexture              = NULL;
PFNGLTEXIMAGE2DPROC                glTexImage2D               = NULL;
PFNGLTEXPARAMETERIPROC             glTexParameteri            = NULL;
PFNGLDELETETEXTURESPROC            glDeleteTextures           = NULL;
PFNGLACTIVETEXTUREPROC             glActiveTexture            = NULL;

PFNGLGENBUFFERSPROC                glGenBuffers               = NULL;
PFNGLBINDBUFFERPROC                glBindBuffer               = NULL;
PFNGLBUFFERDATAPROC                glBufferData               = NULL;
PFNGLDELETEBUFFERSPROC             glDeleteBuffers            = NULL;

PFNGLCREATESHADERPROC              glCreateShader             = NULL;
PFNGLSHADERSOURCEPROC              glShaderSource             = NULL;
PFNGLCOMPILESHADERPROC             glCompileShader            = NULL;
PFNGLGETSHADERIVPROC               glGetShaderiv              = NULL;
PFNGLGETSHADERINFOLOGPROC          glGetShaderInfoLog         = NULL;
PFNGLDELETESHADERPROC              glDeleteShader             = NULL;

PFNGLCREATEPROGRAMPROC             glCreateProgram            = NULL;
PFNGLATTACHSHADERPROC              glAttachShader             = NULL;
PFNGLLINKPROGRAMPROC               glLinkProgram              = NULL;
PFNGLGETPROGRAMIVPROC              glGetProgramiv             = NULL;
PFNGLGETPROGRAMINFOLOGPROC         glGetProgramInfoLog        = NULL;
PFNGLUSEPROGRAMPROC                glUseProgram               = NULL;
PFNGLDELETEPROGRAMPROC             glDeleteProgram            = NULL;

PFNGLGETATTRIBLOCATIONPROC         glGetAttribLocation        = NULL;
PFNGLENABLEVERTEXATTRIBARRAYPROC   glEnableVertexAttribArray  = NULL;
PFNGLDISABLEVERTEXATTRIBARRAYPROC  glDisableVertexAttribArray = NULL;
PFNGLVERTEXATTRIBPOINTERPROC       glVertexAttribPointer      = NULL;

PFNGLGETUNIFORMLOCATIONPROC        glGetUniformLocation       = NULL;
PFNGLUNIFORM1IPROC                 glUniform1i                = NULL;
PFNGLUNIFORM4FPROC                 glUniform4f                = NULL;
PFNGLUNIFORM4FVPROC                glUniform4fv               = NULL;

PFNGLGENVERTEXARRAYSPROC           glGenVertexArrays          = NULL;
PFNGLBINDVERTEXARRAYPROC           glBindVertexArray          = NULL;
PFNGLDELETEVERTEXARRAYSPROC        glDeleteVertexArrays       = NULL;

PFNGLDEBUGMESSAGECALLBACKPROC      glDebugMessageCallback     = NULL;


int gladLoadGL(GLADloadfunc load) {
    int ok = 1;

#define LOAD_REQ(type, name)                                   \
    name = (type)load(#name);                                  \
    if (!name) {                                               \
        fprintf(stderr, "glad: required symbol not found: " #name "\n"); \
        ok = 0;                                                \
    }

#define LOAD_OPT(type, name)  name = (type)load(#name);

    // GL 1.0 / 1.1
    LOAD_REQ(PFNGLCLEARPROC,             glClear)
    LOAD_REQ(PFNGLCLEARCOLORPROC,        glClearColor)
    LOAD_REQ(PFNGLENABLEPROC,            glEnable)
    LOAD_REQ(PFNGLDISABLEPROC,           glDisable)
    LOAD_REQ(PFNGLBLENDFUNCPROC,         glBlendFunc)
    LOAD_REQ(PFNGLDEPTHMASKPROC,         glDepthMask)
    LOAD_REQ(PFNGLDEPTHFUNCPROC,         glDepthFunc)
    LOAD_REQ(PFNGLVIEWPORTPROC,          glViewport)
    LOAD_REQ(PFNGLSCISSORPROC,           glScissor)
    LOAD_REQ(PFNGLPOLYGONOFFSETPROC,     glPolygonOffset)
    LOAD_REQ(PFNGLDRAWARRAYSPROC,        glDrawArrays)
    LOAD_REQ(PFNGLGENTEXTURESPROC,       glGenTextures)
    LOAD_REQ(PFNGLBINDTEXTUREPROC,       glBindTexture)
    LOAD_REQ(PFNGLTEXIMAGE2DPROC,        glTexImage2D)
    LOAD_REQ(PFNGLTEXPARAMETERIPROC,     glTexParameteri)
    LOAD_REQ(PFNGLDELETETEXTURESPROC,    glDeleteTextures)

    // GL 1.3
    LOAD_REQ(PFNGLACTIVETEXTUREPROC,     glActiveTexture)

    // GL 1.5
    LOAD_REQ(PFNGLGENBUFFERSPROC,        glGenBuffers)
    LOAD_REQ(PFNGLBINDBUFFERPROC,        glBindBuffer)
    LOAD_REQ(PFNGLBUFFERDATAPROC,        glBufferData)
    LOAD_REQ(PFNGLDELETEBUFFERSPROC,     glDeleteBuffers)

    // GL 2.0
    LOAD_REQ(PFNGLCREATESHADERPROC,              glCreateShader)
    LOAD_REQ(PFNGLSHADERSOURCEPROC,              glShaderSource)
    LOAD_REQ(PFNGLCOMPILESHADERPROC,             glCompileShader)
    LOAD_REQ(PFNGLGETSHADERIVPROC,               glGetShaderiv)
    LOAD_REQ(PFNGLGETSHADERINFOLOGPROC,           glGetShaderInfoLog)
    LOAD_REQ(PFNGLDELETESHADERPROC,              glDeleteShader)
    LOAD_REQ(PFNGLCREATEPROGRAMPROC,             glCreateProgram)
    LOAD_REQ(PFNGLATTACHSHADERPROC,              glAttachShader)
    LOAD_REQ(PFNGLLINKPROGRAMPROC,               glLinkProgram)
    LOAD_REQ(PFNGLGETPROGRAMIVPROC,              glGetProgramiv)
    LOAD_REQ(PFNGLGETPROGRAMINFOLOGPROC,          glGetProgramInfoLog)
    LOAD_REQ(PFNGLUSEPROGRAMPROC,                glUseProgram)
    LOAD_REQ(PFNGLDELETEPROGRAMPROC,             glDeleteProgram)
    LOAD_REQ(PFNGLGETATTRIBLOCATIONPROC,         glGetAttribLocation)
    LOAD_REQ(PFNGLENABLEVERTEXATTRIBARRAYPROC,   glEnableVertexAttribArray)
    LOAD_REQ(PFNGLDISABLEVERTEXATTRIBARRAYPROC,  glDisableVertexAttribArray)
    LOAD_REQ(PFNGLVERTEXATTRIBPOINTERPROC,       glVertexAttribPointer)
    LOAD_REQ(PFNGLGETUNIFORMLOCATIONPROC,        glGetUniformLocation)
    LOAD_REQ(PFNGLUNIFORM1IPROC,                 glUniform1i)
    LOAD_REQ(PFNGLUNIFORM4FPROC,                 glUniform4f)
    LOAD_REQ(PFNGLUNIFORM4FVPROC,                glUniform4fv)

    // Core GL 3.0
    LOAD_REQ(PFNGLGENVERTEXARRAYSPROC,    glGenVertexArrays)
    LOAD_REQ(PFNGLBINDVERTEXARRAYPROC,    glBindVertexArray)
    LOAD_REQ(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)

    // GL 4.3 - optional; NULL on macOS
    LOAD_OPT(PFNGLDEBUGMESSAGECALLBACKPROC, glDebugMessageCallback)

#undef LOAD_REQ
#undef LOAD_OPT

    return ok;
}
