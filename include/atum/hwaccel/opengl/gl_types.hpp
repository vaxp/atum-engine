#pragma once

#include <cstdint>
#include <cstddef>

namespace atum::gl {

// ============================================================================
// EGL 1.5 & Extension Clean-Room Types
// ============================================================================
using EGLDisplay = void*;
using EGLConfig = void*;
using EGLContext = void*;
using EGLSurface = void*;
using EGLClientBuffer = void*;
using EGLImageKHR = void*;
using EGLSyncKHR = void*;
using EGLDeviceEXT = void*;

using EGLint = int32_t;
using EGLBoolean = unsigned int;
using EGLenum = unsigned int;
using EGLAttrib = intptr_t;

inline constexpr EGLDisplay EGL_NO_DISPLAY_PTR = nullptr;
inline constexpr EGLContext EGL_NO_CONTEXT_PTR = nullptr;
inline constexpr EGLSurface EGL_NO_SURFACE_PTR = nullptr;
inline constexpr EGLImageKHR EGL_NO_IMAGE_KHR_PTR = nullptr;
inline constexpr EGLDisplay EGL_DEFAULT_DISPLAY_PTR = nullptr;

inline constexpr EGLBoolean EGL_FALSE_VAL = 0;
inline constexpr EGLBoolean EGL_TRUE_VAL = 1;

inline constexpr EGLint EGL_SUCCESS_VAL = 0x3000;
inline constexpr EGLint EGL_BUFFER_SIZE_VAL = 0x3020;
inline constexpr EGLint EGL_RED_SIZE_VAL = 0x3024;
inline constexpr EGLint EGL_GREEN_SIZE_VAL = 0x3023;
inline constexpr EGLint EGL_BLUE_SIZE_VAL = 0x3022;
inline constexpr EGLint EGL_ALPHA_SIZE_VAL = 0x3021;
inline constexpr EGLint EGL_RENDERABLE_TYPE_VAL = 0x3040;
inline constexpr EGLint EGL_SURFACE_TYPE_VAL = 0x3033;
inline constexpr EGLint EGL_PBUFFER_BIT_VAL = 0x0001;
inline constexpr EGLint EGL_OPENGL_ES2_BIT_VAL = 0x0004;
inline constexpr EGLint EGL_OPENGL_ES3_BIT_VAL = 0x00000040;
inline constexpr EGLint EGL_OPENGL_BIT_VAL = 0x0008;
inline constexpr EGLint EGL_NONE_VAL = 0x3038;
inline constexpr EGLint EGL_WIDTH_VAL = 0x3057;
inline constexpr EGLint EGL_HEIGHT_VAL = 0x3056;
inline constexpr EGLint EGL_EXTENSIONS_VAL = 0x3055;
inline constexpr EGLint EGL_DRAW = 0x3059;
inline constexpr EGLint EGL_READ = 0x305A;
inline constexpr EGLint EGL_CONTEXT_CLIENT_VERSION_VAL = 0x3098;
inline constexpr EGLint EGL_CONTEXT_MAJOR_VERSION_VAL = 0x3098;
inline constexpr EGLint EGL_CONTEXT_MINOR_VERSION_VAL = 0x30FB;
inline constexpr EGLint EGL_CONTEXT_OPENGL_PROFILE_MASK_VAL = 0x30FD;
inline constexpr EGLint EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_VAL = 0x00000001;

inline constexpr EGLenum EGL_OPENGL_API_VAL = 0x30A2;
inline constexpr EGLenum EGL_OPENGL_ES_API_VAL = 0x30A0;

// Platform & DMA-BUF Extensions
inline constexpr EGLenum EGL_PLATFORM_DEVICE_EXT_VAL = 0x313F;
inline constexpr EGLenum EGL_PLATFORM_SURFACELESS_MESA_VAL = 0x31DD;
inline constexpr EGLenum EGL_PLATFORM_GBM_MESA_VAL = 0x31D7;
inline constexpr EGLenum EGL_PLATFORM_WAYLAND_EXT_VAL = 0x31D8;
inline constexpr EGLenum EGL_PLATFORM_X11_EXT_VAL = 0x31D5;

inline constexpr EGLenum EGL_LINUX_DMA_BUF_EXT_VAL = 0x3270;
inline constexpr EGLint EGL_LINUX_DRM_FOURCC_EXT_VAL = 0x3271;
inline constexpr EGLint EGL_DMA_BUF_PLANE0_FD_EXT_VAL = 0x3272;
inline constexpr EGLint EGL_DMA_BUF_PLANE0_OFFSET_EXT_VAL = 0x3273;
inline constexpr EGLint EGL_DMA_BUF_PLANE0_PITCH_EXT_VAL = 0x3274;
inline constexpr EGLint EGL_DMA_BUF_PLANE1_FD_EXT_VAL = 0x3275;
inline constexpr EGLint EGL_DMA_BUF_PLANE1_OFFSET_EXT_VAL = 0x3276;
inline constexpr EGLint EGL_DMA_BUF_PLANE1_PITCH_EXT_VAL = 0x3277;
inline constexpr EGLint EGL_DMA_BUF_PLANE2_FD_EXT_VAL = 0x3278;
inline constexpr EGLint EGL_DMA_BUF_PLANE2_OFFSET_EXT_VAL = 0x3279;
inline constexpr EGLint EGL_DMA_BUF_PLANE2_PITCH_EXT_VAL = 0x327A;
inline constexpr EGLint EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT_VAL = 0x3443;
inline constexpr EGLint EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT_VAL = 0x3444;
inline constexpr EGLint EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT_VAL = 0x3445;
inline constexpr EGLint EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT_VAL = 0x3446;
inline constexpr EGLint EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT_VAL = 0x3447;
inline constexpr EGLint EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT_VAL = 0x3448;
inline constexpr EGLint EGL_IMAGE_PRESERVED_KHR_VAL = 0x30D2;
inline constexpr EGLenum EGL_GL_TEXTURE_2D_KHR_VAL = 0x30B1;

// ============================================================================
// OpenGL & OpenGL ES Clean-Room Types & Constants
// ============================================================================
using GLenum = unsigned int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLbyte = signed char;
using GLshort = short;
using GLint = int;
using GLsizei = int;
using GLubyte = unsigned char;
using GLushort = unsigned short;
using GLuint = unsigned int;
using GLfloat = float;
using GLclampf = float;
using GLdouble = double;
using GLclampd = double;
using GLvoid = void;
using GLchar = char;
using GLintptr = ptrdiff_t;
using GLsizeiptr = ptrdiff_t;
using GLsync = void*;

inline constexpr GLboolean GL_FALSE_VAL = 0;
inline constexpr GLboolean GL_TRUE_VAL = 1;

inline constexpr GLenum GL_BYTE_VAL = 0x1400;
inline constexpr GLenum GL_UNSIGNED_BYTE_VAL = 0x1401;
inline constexpr GLenum GL_SHORT_VAL = 0x1402;
inline constexpr GLenum GL_UNSIGNED_SHORT_VAL = 0x1403;
inline constexpr GLenum GL_INT_VAL = 0x1404;
inline constexpr GLenum GL_UNSIGNED_INT_VAL = 0x1405;
inline constexpr GLenum GL_FLOAT_VAL = 0x1406;

inline constexpr GLenum GL_RGB_VAL = 0x1907;
inline constexpr GLenum GL_RGBA_VAL = 0x1908;
inline constexpr GLenum GL_RGBA8_VAL = 0x8058;
inline constexpr GLenum GL_BGRA_VAL = 0x80E1;
inline constexpr GLenum GL_RED_VAL = 0x1903;
inline constexpr GLenum GL_RG_VAL = 0x8227;
inline constexpr GLenum GL_R8_VAL = 0x8229;
inline constexpr GLenum GL_RG8_VAL = 0x822B;

inline constexpr GLenum GL_TEXTURE_2D_VAL = 0x0DE1;
inline constexpr GLenum GL_TEXTURE_EXTERNAL_OES_VAL = 0x8D65;
inline constexpr GLenum GL_TEXTURE_BINDING_2D_VAL = 0x8069;
inline constexpr GLenum GL_TEXTURE_MAG_FILTER_VAL = 0x2800;
inline constexpr GLenum GL_TEXTURE_MIN_FILTER_VAL = 0x2801;
inline constexpr GLenum GL_TEXTURE_WRAP_S_VAL = 0x2802;
inline constexpr GLenum GL_TEXTURE_WRAP_T_VAL = 0x2803;
inline constexpr GLint GL_NEAREST_VAL = 0x2600;
inline constexpr GLint GL_LINEAR_VAL = 0x2601;
inline constexpr GLint GL_CLAMP_TO_EDGE_VAL = 0x812F;

inline constexpr GLenum GL_FRAMEBUFFER_VAL = 0x8D40;
inline constexpr GLenum GL_READ_FRAMEBUFFER_VAL = 0x8CA8;
inline constexpr GLenum GL_DRAW_FRAMEBUFFER_VAL = 0x8CA9;
inline constexpr GLenum GL_COLOR_ATTACHMENT0_VAL = 0x8CE0;
inline constexpr GLenum GL_FRAMEBUFFER_COMPLETE_VAL = 0x8CD5;
inline constexpr GLenum GL_FRAMEBUFFER_BINDING_VAL = 0x8CA6;

inline constexpr GLenum GL_ARRAY_BUFFER_VAL = 0x8892;
inline constexpr GLenum GL_ELEMENT_ARRAY_BUFFER_VAL = 0x8893;
inline constexpr GLenum GL_STATIC_DRAW_VAL = 0x88E4;
inline constexpr GLenum GL_DYNAMIC_DRAW_VAL = 0x88E8;
inline constexpr GLenum GL_STREAM_DRAW_VAL = 0x88E0;

inline constexpr GLenum GL_PIXEL_PACK_BUFFER_VAL = 0x88EB;
inline constexpr GLenum GL_STREAM_READ_VAL = 0x88E1;
inline constexpr GLenum GL_READ_ONLY_VAL = 0x88B8;

inline constexpr GLenum GL_VERTEX_SHADER_VAL = 0x8B31;
inline constexpr GLenum GL_FRAGMENT_SHADER_VAL = 0x8B30;
inline constexpr GLenum GL_COMPILE_STATUS_VAL = 0x8B81;
inline constexpr GLenum GL_LINK_STATUS_VAL = 0x8B82;
inline constexpr GLenum GL_INFO_LOG_LENGTH_VAL = 0x8B84;

inline constexpr GLenum GL_TRIANGLES_VAL = 0x0004;
inline constexpr GLenum GL_TRIANGLE_STRIP_VAL = 0x0005;
inline constexpr GLbitfield GL_COLOR_BUFFER_BIT_VAL = 0x00004000;
inline constexpr GLenum GL_VIEWPORT_VAL = 0x0BA2;
inline constexpr GLenum GL_NO_ERROR_VAL = 0;

// DRM FourCC Definitions
inline constexpr uint32_t DRM_FOURCC_ARGB8888 = 0x34325241;
inline constexpr uint32_t DRM_FOURCC_XRGB8888 = 0x34325258;
inline constexpr uint32_t DRM_FOURCC_ABGR8888 = 0x34324241;
inline constexpr uint32_t DRM_FOURCC_XBGR8888 = 0x34324258;
inline constexpr uint32_t DRM_FOURCC_RGBA8888 = 0x34324152;
inline constexpr uint32_t DRM_FOURCC_RGBX8888 = 0x34325852;
inline constexpr uint32_t DRM_FOURCC_NV12     = 0x3231564e;
inline constexpr uint32_t DRM_FOURCC_NV21     = 0x3132564e;
inline constexpr uint32_t DRM_FOURCC_P010     = 0x30313050;
inline constexpr uint32_t DRM_FOURCC_YUV420   = 0x32315559;

inline constexpr uint64_t DRM_FORMAT_MOD_LINEAR_VAL = 0ULL;
inline constexpr uint64_t DRM_FORMAT_MOD_INVALID_VAL = ((1ULL << 56) - 1);

// ============================================================================
// EGL Function Pointer Prototypes
// ============================================================================
using PFN_eglGetDisplay = EGLDisplay (*)(void* native_display);
using PFN_eglInitialize = EGLBoolean (*)(EGLDisplay dpy, EGLint* major, EGLint* minor);
using PFN_eglTerminate = EGLBoolean (*)(EGLDisplay dpy);
using PFN_eglQueryString = const char* (*)(EGLDisplay dpy, EGLint name);
using PFN_eglGetConfigs = EGLBoolean (*)(EGLDisplay dpy, EGLConfig* configs, EGLint config_size, EGLint* num_config);
using PFN_eglChooseConfig = EGLBoolean (*)(EGLDisplay dpy, const EGLint* attrib_list, EGLConfig* configs, EGLint config_size, EGLint* num_config);
using PFN_eglGetConfigAttrib = EGLBoolean (*)(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint* value);
using PFN_eglCreateContext = EGLContext (*)(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint* attrib_list);
using PFN_eglDestroyContext = EGLBoolean (*)(EGLDisplay dpy, EGLContext ctx);
using PFN_eglMakeCurrent = EGLBoolean (*)(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);
using PFN_eglGetCurrentContext = EGLContext (*)();
using PFN_eglGetCurrentSurface = EGLSurface (*)(EGLint readdraw);
using PFN_eglGetCurrentDisplay = EGLDisplay (*)();
using PFN_eglCreatePbufferSurface = EGLSurface (*)(EGLDisplay dpy, EGLConfig config, const EGLint* attrib_list);
using PFN_eglDestroySurface = EGLBoolean (*)(EGLDisplay dpy, EGLSurface surface);
using PFN_eglGetError = EGLint (*)();
using PFN_eglBindAPI = EGLBoolean (*)(EGLenum api);
using PFN_eglQueryAPI = EGLenum (*)();
using PFN_eglWaitGL = EGLBoolean (*)();
using PFN_eglWaitNative = EGLBoolean (*)(EGLint engine);
using PFN_eglGetProcAddress = void* (*)(const char* procname);

// Extension Function Pointers
using PFN_eglGetPlatformDisplayEXT = EGLDisplay (*)(EGLenum platform, void* native_display, const EGLint* attrib_list);
using PFN_eglCreateImageKHR = EGLImageKHR (*)(EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLint* attrib_list);
using PFN_eglDestroyImageKHR = EGLBoolean (*)(EGLDisplay dpy, EGLImageKHR image);
using PFN_eglQueryDmaBufFormatsEXT = EGLBoolean (*)(EGLDisplay dpy, EGLint max_formats, EGLint* formats, EGLint* num_formats);
using PFN_eglQueryDmaBufModifiersEXT = EGLBoolean (*)(EGLDisplay dpy, EGLint format, EGLint max_modifiers, uint64_t* modifiers, EGLBoolean* external_only, EGLint* num_modifiers);

// ============================================================================
// OpenGL Function Pointer Prototypes
// ============================================================================
using PFN_glGetString = const GLubyte* (*)(GLenum name);
using PFN_glGetError = GLenum (*)();
using PFN_glViewport = void (*)(GLint x, GLint y, GLsizei width, GLsizei height);
using PFN_glClearColor = void (*)(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
using PFN_glClear = void (*)(GLbitfield mask);

using PFN_glGenTextures = void (*)(GLsizei n, GLuint* textures);
using PFN_glDeleteTextures = void (*)(GLsizei n, const GLuint* textures);
using PFN_glBindTexture = void (*)(GLenum target, GLuint texture);
using PFN_glTexParameteri = void (*)(GLenum target, GLenum pname, GLint param);
using PFN_glTexImage2D = void (*)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void* pixels);
using PFN_glActiveTexture = void (*)(GLenum texture);

using PFN_glGenFramebuffers = void (*)(GLsizei n, GLuint* framebuffers);
using PFN_glDeleteFramebuffers = void (*)(GLsizei n, const GLuint* framebuffers);
using PFN_glBindFramebuffer = void (*)(GLenum target, GLuint framebuffer);
using PFN_glFramebufferTexture2D = void (*)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
using PFN_glCheckFramebufferStatus = GLenum (*)(GLenum target);

using PFN_glCreateShader = GLuint (*)(GLenum type);
using PFN_glDeleteShader = void (*)(GLuint shader);
using PFN_glShaderSource = void (*)(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length);
using PFN_glCompileShader = void (*)(GLuint shader);
using PFN_glGetShaderiv = void (*)(GLuint shader, GLenum pname, GLint* params);
using PFN_glGetShaderInfoLog = void (*)(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);

using PFN_glCreateProgram = GLuint (*)();
using PFN_glDeleteProgram = void (*)(GLuint program);
using PFN_glAttachShader = void (*)(GLuint program, GLuint shader);
using PFN_glDetachShader = void (*)(GLuint program, GLuint shader);
using PFN_glLinkProgram = void (*)(GLuint program);
using PFN_glUseProgram = void (*)(GLuint program);
using PFN_glGetProgramiv = void (*)(GLuint program, GLenum pname, GLint* params);
using PFN_glGetProgramInfoLog = void (*)(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
using PFN_glGetUniformLocation = GLint (*)(GLuint program, const GLchar* name);
using PFN_glUniform1i = void (*)(GLint location, GLint v0);
using PFN_glUniform1f = void (*)(GLint location, GLfloat v0);
using PFN_glUniformMatrix3fv = void (*)(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
using PFN_glUniformMatrix4fv = void (*)(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);

using PFN_glGenBuffers = void (*)(GLsizei n, GLuint* buffers);
using PFN_glDeleteBuffers = void (*)(GLsizei n, const GLuint* buffers);
using PFN_glBindBuffer = void (*)(GLenum target, GLuint buffer);
using PFN_glBufferData = void (*)(GLenum target, GLsizeiptr size, const void* data, GLenum usage);

using PFN_glGenVertexArrays = void (*)(GLsizei n, GLuint* arrays);
using PFN_glDeleteVertexArrays = void (*)(GLsizei n, const GLuint* arrays);
using PFN_glBindVertexArray = void (*)(GLuint array);
using PFN_glEnableVertexAttribArray = void (*)(GLuint index);
using PFN_glDisableVertexAttribArray = void (*)(GLuint index);
using PFN_glVertexAttribPointer = void (*)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer);

using PFN_glDrawArrays = void (*)(GLenum mode, GLint first, GLsizei count);
using PFN_glFinish = void (*)();
using PFN_glFlush = void (*)();
using PFN_glReadPixels = void (*)(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void* pixels);

// Extension Function Pointers
using PFN_glEGLImageTargetTexture2DOES = void (*)(GLenum target, EGLImageKHR image);

} // namespace atum::gl
