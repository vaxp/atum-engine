#pragma once

#include <string_view>
#include <string>
#include "atum/hwaccel/opengl/gl_types.hpp"

namespace atum::gl {

/// Clean-room dynamic function dispatcher for EGL and OpenGL / GLES.
/// Dynamically resolves all EGL 1.5, DMA-BUF interop extensions, and OpenGL entry points without hard build-time linking.
class GlDispatcher {
public:
    GlDispatcher() = default;
    ~GlDispatcher();

    // Non-copyable, movable
    GlDispatcher(const GlDispatcher&) = delete;
    GlDispatcher& operator=(const GlDispatcher&) = delete;
    GlDispatcher(GlDispatcher&& other) noexcept;
    GlDispatcher& operator=(GlDispatcher&& other) noexcept;

    /// Loads global EGL symbols from libEGL.so.1 via dlopen.
    [[nodiscard]] bool load_egl() noexcept;

    /// Loads OpenGL/GLES symbols and EGL extensions after an EGL display is initialized.
    [[nodiscard]] bool load_gl(EGLDisplay dpy = EGL_NO_DISPLAY_PTR) noexcept;

    /// Releases dynamic library handles.
    void unload() noexcept;

    [[nodiscard]] bool is_egl_loaded() const noexcept { return egl_lib_handle_ != nullptr; }
    [[nodiscard]] bool is_gl_loaded() const noexcept { return gl_loaded_; }

    [[nodiscard]] bool has_extension(std::string_view extension_list, std::string_view name) const noexcept;
    [[nodiscard]] bool has_client_extension(std::string_view name) const noexcept;
    [[nodiscard]] bool has_display_extension(EGLDisplay dpy, std::string_view name) const noexcept;

    // --- Core EGL Entry Points ---
    PFN_eglGetDisplay eglGetDisplay{nullptr};
    PFN_eglInitialize eglInitialize{nullptr};
    PFN_eglTerminate eglTerminate{nullptr};
    PFN_eglQueryString eglQueryString{nullptr};
    PFN_eglGetConfigs eglGetConfigs{nullptr};
    PFN_eglChooseConfig eglChooseConfig{nullptr};
    PFN_eglGetConfigAttrib eglGetConfigAttrib{nullptr};
    PFN_eglCreateContext eglCreateContext{nullptr};
    PFN_eglDestroyContext eglDestroyContext{nullptr};
    PFN_eglMakeCurrent eglMakeCurrent{nullptr};
    PFN_eglGetCurrentContext eglGetCurrentContext{nullptr};
    PFN_eglGetCurrentSurface eglGetCurrentSurface{nullptr};
    PFN_eglGetCurrentDisplay eglGetCurrentDisplay{nullptr};
    PFN_eglCreatePbufferSurface eglCreatePbufferSurface{nullptr};
    PFN_eglDestroySurface eglDestroySurface{nullptr};
    PFN_eglGetError eglGetError{nullptr};
    PFN_eglBindAPI eglBindAPI{nullptr};
    PFN_eglQueryAPI eglQueryAPI{nullptr};
    PFN_eglWaitGL eglWaitGL{nullptr};
    PFN_eglWaitNative eglWaitNative{nullptr};
    PFN_eglGetProcAddress eglGetProcAddress{nullptr};

    // --- EGL Extension Entry Points ---
    PFN_eglGetPlatformDisplayEXT eglGetPlatformDisplayEXT{nullptr};
    PFN_eglCreateImageKHR eglCreateImageKHR{nullptr};
    PFN_eglDestroyImageKHR eglDestroyImageKHR{nullptr};
    PFN_eglQueryDmaBufFormatsEXT eglQueryDmaBufFormatsEXT{nullptr};
    PFN_eglQueryDmaBufModifiersEXT eglQueryDmaBufModifiersEXT{nullptr};

    // --- Core OpenGL / GLES Entry Points ---
    PFN_glGetString glGetString{nullptr};
    PFN_glGetError glGetError{nullptr};
    PFN_glViewport glViewport{nullptr};
    PFN_glClearColor glClearColor{nullptr};
    PFN_glClear glClear{nullptr};

    PFN_glGenTextures glGenTextures{nullptr};
    PFN_glDeleteTextures glDeleteTextures{nullptr};
    PFN_glBindTexture glBindTexture{nullptr};
    PFN_glTexParameteri glTexParameteri{nullptr};
    PFN_glTexImage2D glTexImage2D{nullptr};
    PFN_glActiveTexture glActiveTexture{nullptr};

    PFN_glGenFramebuffers glGenFramebuffers{nullptr};
    PFN_glDeleteFramebuffers glDeleteFramebuffers{nullptr};
    PFN_glBindFramebuffer glBindFramebuffer{nullptr};
    PFN_glFramebufferTexture2D glFramebufferTexture2D{nullptr};
    PFN_glCheckFramebufferStatus glCheckFramebufferStatus{nullptr};

    PFN_glCreateShader glCreateShader{nullptr};
    PFN_glDeleteShader glDeleteShader{nullptr};
    PFN_glShaderSource glShaderSource{nullptr};
    PFN_glCompileShader glCompileShader{nullptr};
    PFN_glGetShaderiv glGetShaderiv{nullptr};
    PFN_glGetShaderInfoLog glGetShaderInfoLog{nullptr};

    PFN_glCreateProgram glCreateProgram{nullptr};
    PFN_glDeleteProgram glDeleteProgram{nullptr};
    PFN_glAttachShader glAttachShader{nullptr};
    PFN_glDetachShader glDetachShader{nullptr};
    PFN_glLinkProgram glLinkProgram{nullptr};
    PFN_glUseProgram glUseProgram{nullptr};
    PFN_glGetProgramiv glGetProgramiv{nullptr};
    PFN_glGetProgramInfoLog glGetProgramInfoLog{nullptr};
    PFN_glGetUniformLocation glGetUniformLocation{nullptr};
    PFN_glUniform1i glUniform1i{nullptr};
    PFN_glUniform1f glUniform1f{nullptr};
    PFN_glUniformMatrix3fv glUniformMatrix3fv{nullptr};
    PFN_glUniformMatrix4fv glUniformMatrix4fv{nullptr};

    PFN_glGenBuffers glGenBuffers{nullptr};
    PFN_glDeleteBuffers glDeleteBuffers{nullptr};
    PFN_glBindBuffer glBindBuffer{nullptr};
    PFN_glBufferData glBufferData{nullptr};

    PFN_glGenVertexArrays glGenVertexArrays{nullptr};
    PFN_glDeleteVertexArrays glDeleteVertexArrays{nullptr};
    PFN_glBindVertexArray glBindVertexArray{nullptr};
    PFN_glEnableVertexAttribArray glEnableVertexAttribArray{nullptr};
    PFN_glDisableVertexAttribArray glDisableVertexAttribArray{nullptr};
    PFN_glVertexAttribPointer glVertexAttribPointer{nullptr};

    PFN_glDrawArrays glDrawArrays{nullptr};
    PFN_glFinish glFinish{nullptr};
    PFN_glFlush glFlush{nullptr};
    PFN_glReadPixels glReadPixels{nullptr};

    // --- GL Extension Entry Points ---
    PFN_glEGLImageTargetTexture2DOES glEGLImageTargetTexture2DOES{nullptr};

private:
    void* egl_lib_handle_{nullptr};
    void* gl_lib_handle_{nullptr};
    bool gl_loaded_{false};
    std::string client_extensions_;
};

} // namespace atum::gl
