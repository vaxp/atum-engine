#include "atum/hwaccel/opengl/gl_dispatcher.hpp"

#include <dlfcn.h>
#include <utility>
#include <cstring>

namespace atum::gl {

GlDispatcher::~GlDispatcher() {
    unload();
}

GlDispatcher::GlDispatcher(GlDispatcher&& other) noexcept {
    *this = std::move(other);
}

GlDispatcher& GlDispatcher::operator=(GlDispatcher&& other) noexcept {
    if (this != &other) {
        unload();

        egl_lib_handle_ = other.egl_lib_handle_;
        gl_lib_handle_ = other.gl_lib_handle_;
        gl_loaded_ = other.gl_loaded_;
        client_extensions_ = std::move(other.client_extensions_);

        other.egl_lib_handle_ = nullptr;
        other.gl_lib_handle_ = nullptr;
        other.gl_loaded_ = false;

#define MOVE_PTR(fn) fn = other.fn; other.fn = nullptr
        MOVE_PTR(eglGetDisplay);
        MOVE_PTR(eglInitialize);
        MOVE_PTR(eglTerminate);
        MOVE_PTR(eglQueryString);
        MOVE_PTR(eglGetConfigs);
        MOVE_PTR(eglChooseConfig);
        MOVE_PTR(eglGetConfigAttrib);
        MOVE_PTR(eglCreateContext);
        MOVE_PTR(eglDestroyContext);
        MOVE_PTR(eglMakeCurrent);
        MOVE_PTR(eglGetCurrentContext);
        MOVE_PTR(eglGetCurrentSurface);
        MOVE_PTR(eglGetCurrentDisplay);
        MOVE_PTR(eglCreatePbufferSurface);
        MOVE_PTR(eglDestroySurface);
        MOVE_PTR(eglGetError);
        MOVE_PTR(eglBindAPI);
        MOVE_PTR(eglQueryAPI);
        MOVE_PTR(eglWaitGL);
        MOVE_PTR(eglWaitNative);
        MOVE_PTR(eglGetProcAddress);

        MOVE_PTR(eglGetPlatformDisplayEXT);
        MOVE_PTR(eglCreateImageKHR);
        MOVE_PTR(eglDestroyImageKHR);
        MOVE_PTR(eglQueryDmaBufFormatsEXT);
        MOVE_PTR(eglQueryDmaBufModifiersEXT);

        MOVE_PTR(glGetString);
        MOVE_PTR(glGetError);
        MOVE_PTR(glViewport);
        MOVE_PTR(glClearColor);
        MOVE_PTR(glClear);

        MOVE_PTR(glGenTextures);
        MOVE_PTR(glDeleteTextures);
        MOVE_PTR(glBindTexture);
        MOVE_PTR(glTexParameteri);
        MOVE_PTR(glTexImage2D);
        MOVE_PTR(glActiveTexture);

        MOVE_PTR(glGenFramebuffers);
        MOVE_PTR(glDeleteFramebuffers);
        MOVE_PTR(glBindFramebuffer);
        MOVE_PTR(glFramebufferTexture2D);
        MOVE_PTR(glCheckFramebufferStatus);

        MOVE_PTR(glCreateShader);
        MOVE_PTR(glDeleteShader);
        MOVE_PTR(glShaderSource);
        MOVE_PTR(glCompileShader);
        MOVE_PTR(glGetShaderiv);
        MOVE_PTR(glGetShaderInfoLog);

        MOVE_PTR(glCreateProgram);
        MOVE_PTR(glDeleteProgram);
        MOVE_PTR(glAttachShader);
        MOVE_PTR(glDetachShader);
        MOVE_PTR(glLinkProgram);
        MOVE_PTR(glUseProgram);
        MOVE_PTR(glGetProgramiv);
        MOVE_PTR(glGetProgramInfoLog);
        MOVE_PTR(glGetUniformLocation);
        MOVE_PTR(glUniform1i);
        MOVE_PTR(glUniform1f);
        MOVE_PTR(glUniformMatrix3fv);
        MOVE_PTR(glUniformMatrix4fv);

        MOVE_PTR(glGenBuffers);
        MOVE_PTR(glDeleteBuffers);
        MOVE_PTR(glBindBuffer);
        MOVE_PTR(glBufferData);

        MOVE_PTR(glGenVertexArrays);
        MOVE_PTR(glDeleteVertexArrays);
        MOVE_PTR(glBindVertexArray);
        MOVE_PTR(glEnableVertexAttribArray);
        MOVE_PTR(glDisableVertexAttribArray);
        MOVE_PTR(glVertexAttribPointer);

        MOVE_PTR(glDrawArrays);
        MOVE_PTR(glFinish);
        MOVE_PTR(glFlush);
        MOVE_PTR(glReadPixels);

        MOVE_PTR(glEGLImageTargetTexture2DOES);
#undef MOVE_PTR
    }
    return *this;
}

void GlDispatcher::unload() noexcept {
    if (gl_lib_handle_) {
        dlclose(gl_lib_handle_);
        gl_lib_handle_ = nullptr;
    }
    if (egl_lib_handle_) {
        dlclose(egl_lib_handle_);
        egl_lib_handle_ = nullptr;
    }
    gl_loaded_ = false;
    client_extensions_.clear();
}

bool GlDispatcher::load_egl() noexcept {
    if (egl_lib_handle_) {
        return true;
    }

    egl_lib_handle_ = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!egl_lib_handle_) {
        egl_lib_handle_ = dlopen("libEGL.so", RTLD_NOW | RTLD_LOCAL);
    }
    if (!egl_lib_handle_) {
        return false;
    }

#define LOAD_EGL_SYM(name) \
    name = reinterpret_cast<PFN_##name>(dlsym(egl_lib_handle_, #name)); \
    if (!name) { unload(); return false; }

    LOAD_EGL_SYM(eglGetDisplay);
    LOAD_EGL_SYM(eglInitialize);
    LOAD_EGL_SYM(eglTerminate);
    LOAD_EGL_SYM(eglQueryString);
    LOAD_EGL_SYM(eglGetConfigs);
    LOAD_EGL_SYM(eglChooseConfig);
    LOAD_EGL_SYM(eglGetConfigAttrib);
    LOAD_EGL_SYM(eglCreateContext);
    LOAD_EGL_SYM(eglDestroyContext);
    LOAD_EGL_SYM(eglMakeCurrent);
    LOAD_EGL_SYM(eglGetCurrentContext);
    LOAD_EGL_SYM(eglGetCurrentSurface);
    LOAD_EGL_SYM(eglGetCurrentDisplay);
    LOAD_EGL_SYM(eglCreatePbufferSurface);
    LOAD_EGL_SYM(eglDestroySurface);
    LOAD_EGL_SYM(eglGetError);
    LOAD_EGL_SYM(eglBindAPI);
    LOAD_EGL_SYM(eglQueryAPI);
    LOAD_EGL_SYM(eglWaitGL);
    LOAD_EGL_SYM(eglWaitNative);
    LOAD_EGL_SYM(eglGetProcAddress);
#undef LOAD_EGL_SYM

    // Optional client extensions
    if (eglGetProcAddress) {
        eglGetPlatformDisplayEXT = reinterpret_cast<PFN_eglGetPlatformDisplayEXT>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
        if (!eglGetPlatformDisplayEXT) {
            eglGetPlatformDisplayEXT = reinterpret_cast<PFN_eglGetPlatformDisplayEXT>(
                eglGetProcAddress("eglGetPlatformDisplay"));
        }
    }
    if (!eglGetPlatformDisplayEXT && egl_lib_handle_) {
        eglGetPlatformDisplayEXT = reinterpret_cast<PFN_eglGetPlatformDisplayEXT>(
            dlsym(egl_lib_handle_, "eglGetPlatformDisplayEXT"));
    }

    // Query client extensions
    const char* client_exts = eglQueryString(EGL_NO_DISPLAY_PTR, EGL_EXTENSIONS_VAL);
    if (client_exts) {
        client_extensions_ = client_exts;
    }

    return true;
}

bool GlDispatcher::load_gl(EGLDisplay dpy) noexcept {
    (void)dpy;
    if (gl_loaded_) {
        return true;
    }
    if (!load_egl()) {
        return false;
    }

    // Attempt to open desktop GL or GLES library
    if (!gl_lib_handle_) {
        gl_lib_handle_ = dlopen("libGL.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!gl_lib_handle_) {
            gl_lib_handle_ = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_LOCAL);
        }
        if (!gl_lib_handle_) {
            gl_lib_handle_ = dlopen("libGL.so", RTLD_NOW | RTLD_LOCAL);
        }
    }

    auto resolve_sym = [this](const char* name) -> void* {
        void* ptr = nullptr;
        if (eglGetProcAddress) {
            ptr = eglGetProcAddress(name);
        }
        if (!ptr && gl_lib_handle_) {
            ptr = dlsym(gl_lib_handle_, name);
        }
        if (!ptr && egl_lib_handle_) {
            ptr = dlsym(egl_lib_handle_, name);
        }
        return ptr;
    };

    // Load EGL extensions requiring active display / initialized EGL
    eglCreateImageKHR = reinterpret_cast<PFN_eglCreateImageKHR>(resolve_sym("eglCreateImageKHR"));
    eglDestroyImageKHR = reinterpret_cast<PFN_eglDestroyImageKHR>(resolve_sym("eglDestroyImageKHR"));
    eglQueryDmaBufFormatsEXT = reinterpret_cast<PFN_eglQueryDmaBufFormatsEXT>(resolve_sym("eglQueryDmaBufFormatsEXT"));
    eglQueryDmaBufModifiersEXT = reinterpret_cast<PFN_eglQueryDmaBufModifiersEXT>(resolve_sym("eglQueryDmaBufModifiersEXT"));

    // Load OpenGL function pointers
#define RESOLVE_GL(name) name = reinterpret_cast<PFN_##name>(resolve_sym(#name))

    RESOLVE_GL(glGetString);
    RESOLVE_GL(glGetError);
    RESOLVE_GL(glViewport);
    RESOLVE_GL(glClearColor);
    RESOLVE_GL(glClear);

    RESOLVE_GL(glGenTextures);
    RESOLVE_GL(glDeleteTextures);
    RESOLVE_GL(glBindTexture);
    RESOLVE_GL(glTexParameteri);
    RESOLVE_GL(glTexImage2D);
    RESOLVE_GL(glActiveTexture);

    RESOLVE_GL(glGenFramebuffers);
    RESOLVE_GL(glDeleteFramebuffers);
    RESOLVE_GL(glBindFramebuffer);
    RESOLVE_GL(glFramebufferTexture2D);
    RESOLVE_GL(glCheckFramebufferStatus);

    RESOLVE_GL(glCreateShader);
    RESOLVE_GL(glDeleteShader);
    RESOLVE_GL(glShaderSource);
    RESOLVE_GL(glCompileShader);
    RESOLVE_GL(glGetShaderiv);
    RESOLVE_GL(glGetShaderInfoLog);

    RESOLVE_GL(glCreateProgram);
    RESOLVE_GL(glDeleteProgram);
    RESOLVE_GL(glAttachShader);
    RESOLVE_GL(glDetachShader);
    RESOLVE_GL(glLinkProgram);
    RESOLVE_GL(glUseProgram);
    RESOLVE_GL(glGetProgramiv);
    RESOLVE_GL(glGetProgramInfoLog);
    RESOLVE_GL(glGetUniformLocation);
    RESOLVE_GL(glUniform1i);
    RESOLVE_GL(glUniform1f);
    RESOLVE_GL(glUniformMatrix3fv);
    RESOLVE_GL(glUniformMatrix4fv);

    RESOLVE_GL(glGenBuffers);
    RESOLVE_GL(glDeleteBuffers);
    RESOLVE_GL(glBindBuffer);
    RESOLVE_GL(glBufferData);

    RESOLVE_GL(glGenVertexArrays);
    RESOLVE_GL(glDeleteVertexArrays);
    RESOLVE_GL(glBindVertexArray);
    RESOLVE_GL(glEnableVertexAttribArray);
    RESOLVE_GL(glDisableVertexAttribArray);
    RESOLVE_GL(glVertexAttribPointer);

    RESOLVE_GL(glDrawArrays);
    RESOLVE_GL(glFinish);
    RESOLVE_GL(glFlush);
    RESOLVE_GL(glReadPixels);

    // Extension symbols
    RESOLVE_GL(glEGLImageTargetTexture2DOES);
#undef RESOLVE_GL

    gl_loaded_ = (glGetString != nullptr && glGenTextures != nullptr && glBindTexture != nullptr);
    return gl_loaded_;
}

bool GlDispatcher::has_extension(std::string_view extension_list, std::string_view name) const noexcept {
    if (extension_list.empty() || name.empty()) {
        return false;
    }
    size_t pos = 0;
    while ((pos = extension_list.find(name, pos)) != std::string_view::npos) {
        bool start_ok = (pos == 0 || extension_list[pos - 1] == ' ');
        bool end_ok = (pos + name.size() == extension_list.size() || extension_list[pos + name.size()] == ' ');
        if (start_ok && end_ok) {
            return true;
        }
        pos += name.size();
    }
    return false;
}

bool GlDispatcher::has_client_extension(std::string_view name) const noexcept {
    return has_extension(client_extensions_, name);
}

bool GlDispatcher::has_display_extension(EGLDisplay dpy, std::string_view name) const noexcept {
    if (!eglQueryString || dpy == EGL_NO_DISPLAY_PTR) {
        return false;
    }
    const char* str = eglQueryString(dpy, EGL_EXTENSIONS_VAL);
    if (!str) {
        return false;
    }
    return has_extension(str, name);
}

} // namespace atum::gl
