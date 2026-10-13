#pragma once

#include <memory>
#include <string>
#include <string_view>
#include "atum/core/media_types.hpp"
#include "atum/hwaccel/opengl/gl_types.hpp"
#include "atum/hwaccel/opengl/gl_dispatcher.hpp"

namespace atum::gl {

/// RAII manager for an EGL Display, Context, and optional off-screen Surface.
class EglContext {
public:
    /// RAII Scoped context activation helper.
    class Scope {
    public:
        explicit Scope(const EglContext& ctx) noexcept;
        ~Scope();

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

        [[nodiscard]] bool is_active() const noexcept { return activated_; }

    private:
        const EglContext& ctx_;
        EGLDisplay prev_display_{EGL_NO_DISPLAY_PTR};
        EGLSurface prev_draw_{EGL_NO_SURFACE_PTR};
        EGLSurface prev_read_{EGL_NO_SURFACE_PTR};
        EGLContext prev_context_{EGL_NO_CONTEXT_PTR};
        bool activated_{false};
    };

    EglContext(GlDispatcher& dispatcher) noexcept;
    ~EglContext();

    // Non-copyable, movable
    EglContext(const EglContext&) = delete;
    EglContext& operator=(const EglContext&) = delete;
    EglContext(EglContext&& other) noexcept;
    EglContext& operator=(EglContext&& other) noexcept;

    /// Creates a headless, off-screen EGL context for server-side decoding and zero-copy DMA-BUF interop.
    static MediaResult<std::unique_ptr<EglContext>> create_headless(GlDispatcher& dispatcher);

    /// Activates this context on the calling thread.
    MediaResult<void> make_current() const noexcept;

    /// Unbinds this context from the calling thread.
    void release_current() const noexcept;

    /// Checks if this context is currently active on the calling thread.
    [[nodiscard]] bool is_current() const noexcept;

    [[nodiscard]] bool is_valid() const noexcept {
        return display_ != EGL_NO_DISPLAY_PTR && context_ != EGL_NO_CONTEXT_PTR;
    }

    [[nodiscard]] EGLDisplay display() const noexcept { return display_; }
    [[nodiscard]] EGLConfig config() const noexcept { return config_; }
    [[nodiscard]] EGLContext context() const noexcept { return context_; }
    [[nodiscard]] EGLSurface surface() const noexcept { return surface_; }
    [[nodiscard]] int major_version() const noexcept { return major_version_; }
    [[nodiscard]] int minor_version() const noexcept { return minor_version_; }

    [[nodiscard]] const GlDispatcher& dispatcher() const noexcept { return *dispatcher_; }
    [[nodiscard]] GlDispatcher& dispatcher() noexcept { return *dispatcher_; }

    [[nodiscard]] std::string_view gl_vendor() const noexcept { return gl_vendor_; }
    [[nodiscard]] std::string_view gl_renderer() const noexcept { return gl_renderer_; }
    [[nodiscard]] std::string_view gl_version() const noexcept { return gl_version_; }

    /// Releases all native EGL resources.
    void reset() noexcept;

private:
    GlDispatcher* dispatcher_{nullptr};
    EGLDisplay display_{EGL_NO_DISPLAY_PTR};
    EGLConfig config_{nullptr};
    EGLContext context_{EGL_NO_CONTEXT_PTR};
    EGLSurface surface_{EGL_NO_SURFACE_PTR};

    int major_version_{0};
    int minor_version_{0};

    std::string gl_vendor_;
    std::string gl_renderer_;
    std::string gl_version_;
};

} // namespace atum::gl
