#include "atum/hwaccel/opengl/egl_context.hpp"

#include <vector>
#include <iostream>

namespace atum::gl {

// ============================================================================
// EglContext::Scope Implementation
// ============================================================================

EglContext::Scope::Scope(const EglContext& ctx) noexcept
    : ctx_(ctx) {
    if (!ctx_.is_valid()) {
        return;
    }
    const auto& disp = ctx_.dispatcher();
    if (disp.eglGetCurrentDisplay && disp.eglGetCurrentContext && disp.eglGetCurrentSurface) {
        prev_display_ = disp.eglGetCurrentDisplay();
        prev_context_ = disp.eglGetCurrentContext();
        prev_draw_ = disp.eglGetCurrentSurface(EGL_DRAW);
        prev_read_ = disp.eglGetCurrentSurface(EGL_READ);
    }
    if (ctx_.make_current().is_ok()) {
        activated_ = true;
    }
}

EglContext::Scope::~Scope() {
    if (activated_ && ctx_.is_valid()) {
        const auto& disp = ctx_.dispatcher();
        if (prev_display_ != EGL_NO_DISPLAY_PTR && prev_context_ != EGL_NO_CONTEXT_PTR && disp.eglMakeCurrent) {
            disp.eglMakeCurrent(prev_display_, prev_draw_, prev_read_, prev_context_);
        } else {
            ctx_.release_current();
        }
    }
}

// ============================================================================
// EglContext Implementation
// ============================================================================

EglContext::EglContext(GlDispatcher& dispatcher) noexcept
    : dispatcher_(&dispatcher) {}

EglContext::~EglContext() {
    reset();
}

EglContext::EglContext(EglContext&& other) noexcept {
    *this = std::move(other);
}

EglContext& EglContext::operator=(EglContext&& other) noexcept {
    if (this != &other) {
        reset();

        dispatcher_ = other.dispatcher_;
        display_ = other.display_;
        config_ = other.config_;
        context_ = other.context_;
        surface_ = other.surface_;
        major_version_ = other.major_version_;
        minor_version_ = other.minor_version_;
        gl_vendor_ = std::move(other.gl_vendor_);
        gl_renderer_ = std::move(other.gl_renderer_);
        gl_version_ = std::move(other.gl_version_);

        other.dispatcher_ = nullptr;
        other.display_ = EGL_NO_DISPLAY_PTR;
        other.config_ = nullptr;
        other.context_ = EGL_NO_CONTEXT_PTR;
        other.surface_ = EGL_NO_SURFACE_PTR;
        other.major_version_ = 0;
        other.minor_version_ = 0;
    }
    return *this;
}

void EglContext::reset() noexcept {
    if (!dispatcher_) {
        return;
    }

    if (is_current()) {
        release_current();
    }

    if (display_ != EGL_NO_DISPLAY_PTR) {
        if (surface_ != EGL_NO_SURFACE_PTR && dispatcher_->eglDestroySurface) {
            dispatcher_->eglDestroySurface(display_, surface_);
            surface_ = EGL_NO_SURFACE_PTR;
        }
        if (context_ != EGL_NO_CONTEXT_PTR && dispatcher_->eglDestroyContext) {
            dispatcher_->eglDestroyContext(display_, context_);
            context_ = EGL_NO_CONTEXT_PTR;
        }
        if (dispatcher_->eglTerminate) {
            dispatcher_->eglTerminate(display_);
            display_ = EGL_NO_DISPLAY_PTR;
        }
    }

    config_ = nullptr;
    major_version_ = 0;
    minor_version_ = 0;
    gl_vendor_.clear();
    gl_renderer_.clear();
    gl_version_.clear();
}

MediaResult<std::unique_ptr<EglContext>> EglContext::create_headless(GlDispatcher& dispatcher) {
    if (!dispatcher.load_egl()) {
        return MediaError::HardwareError;
    }

    auto ctx = std::unique_ptr<EglContext>(new EglContext(dispatcher));
    EGLDisplay dpy = EGL_NO_DISPLAY_PTR;

    // Strategy 1: Surfaceless MESA platform (optimal for Linux headless / server)
    if (dispatcher.has_client_extension("EGL_MESA_platform_surfaceless") && dispatcher.eglGetPlatformDisplayEXT) {
        dpy = dispatcher.eglGetPlatformDisplayEXT(
            EGL_PLATFORM_SURFACELESS_MESA_VAL, EGL_DEFAULT_DISPLAY_PTR, nullptr);
    }

    // Strategy 2: Fallback to default display
    if (dpy == EGL_NO_DISPLAY_PTR && dispatcher.eglGetDisplay) {
        dpy = dispatcher.eglGetDisplay(EGL_DEFAULT_DISPLAY_PTR);
    }

    if (dpy == EGL_NO_DISPLAY_PTR) {
        return MediaError::HardwareError;
    }

    EGLint major = 0;
    EGLint minor = 0;
    if (dispatcher.eglInitialize(dpy, &major, &minor) != EGL_TRUE_VAL) {
        return MediaError::HardwareError;
    }

    ctx->display_ = dpy;
    ctx->major_version_ = major;
    ctx->minor_version_ = minor;

    // Attempt to bind Desktop OpenGL, fallback to OpenGL ES
    bool is_gles = false;
    if (dispatcher.eglBindAPI(EGL_OPENGL_API_VAL) != EGL_TRUE_VAL) {
        if (dispatcher.eglBindAPI(EGL_OPENGL_ES_API_VAL) == EGL_TRUE_VAL) {
            is_gles = true;
        } else {
            ctx->reset();
            return MediaError::UnsupportedFormat;
        }
    }

    // Choose Configuration
    const EGLint config_attribs[] = {
        EGL_RENDERABLE_TYPE_VAL, is_gles ? EGL_OPENGL_ES2_BIT_VAL : EGL_OPENGL_BIT_VAL,
        EGL_RED_SIZE_VAL, 8,
        EGL_GREEN_SIZE_VAL, 8,
        EGL_BLUE_SIZE_VAL, 8,
        EGL_ALPHA_SIZE_VAL, 8,
        EGL_SURFACE_TYPE_VAL, EGL_PBUFFER_BIT_VAL,
        EGL_NONE_VAL
    };

    EGLConfig config = nullptr;
    EGLint num_configs = 0;
    if (dispatcher.eglChooseConfig(dpy, config_attribs, &config, 1, &num_configs) != EGL_TRUE_VAL || num_configs == 0) {
        // Fallback with minimal config attributes
        const EGLint fallback_attribs[] = {
            EGL_RENDERABLE_TYPE_VAL, is_gles ? EGL_OPENGL_ES2_BIT_VAL : EGL_OPENGL_BIT_VAL,
            EGL_NONE_VAL
        };
        if (dispatcher.eglChooseConfig(dpy, fallback_attribs, &config, 1, &num_configs) != EGL_TRUE_VAL || num_configs == 0) {
            ctx->reset();
            return MediaError::UnsupportedFormat;
        }
    }
    ctx->config_ = config;

    // Create Context
    EGLContext egl_ctx = EGL_NO_CONTEXT_PTR;
    if (!is_gles) {
        // Desktop OpenGL Core Profile
        const EGLint ctx_attribs_core[] = {
            EGL_CONTEXT_MAJOR_VERSION_VAL, 3,
            EGL_CONTEXT_MINOR_VERSION_VAL, 3,
            EGL_CONTEXT_OPENGL_PROFILE_MASK_VAL, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_VAL,
            EGL_NONE_VAL
        };
        egl_ctx = dispatcher.eglCreateContext(dpy, config, EGL_NO_CONTEXT_PTR, ctx_attribs_core);

        // Fallback to unconstrained desktop profile
        if (egl_ctx == EGL_NO_CONTEXT_PTR) {
            const EGLint ctx_attribs_compat[] = { EGL_NONE_VAL };
            egl_ctx = dispatcher.eglCreateContext(dpy, config, EGL_NO_CONTEXT_PTR, ctx_attribs_compat);
        }
    }

    if (egl_ctx == EGL_NO_CONTEXT_PTR) {
        // Try OpenGL ES 3.0 / 2.0
        dispatcher.eglBindAPI(EGL_OPENGL_ES_API_VAL);
        const EGLint ctx_attribs_gles3[] = {
            EGL_CONTEXT_CLIENT_VERSION_VAL, 3,
            EGL_NONE_VAL
        };
        egl_ctx = dispatcher.eglCreateContext(dpy, config, EGL_NO_CONTEXT_PTR, ctx_attribs_gles3);
        if (egl_ctx == EGL_NO_CONTEXT_PTR) {
            const EGLint ctx_attribs_gles2[] = {
                EGL_CONTEXT_CLIENT_VERSION_VAL, 2,
                EGL_NONE_VAL
            };
            egl_ctx = dispatcher.eglCreateContext(dpy, config, EGL_NO_CONTEXT_PTR, ctx_attribs_gles2);
        }
    }

    if (egl_ctx == EGL_NO_CONTEXT_PTR) {
        ctx->reset();
        return MediaError::HardwareError;
    }
    ctx->context_ = egl_ctx;

    // Create a 1x1 pbuffer surface as fallback surface for platforms that require a surface
    const EGLint pbuffer_attribs[] = {
        EGL_WIDTH_VAL, 1,
        EGL_HEIGHT_VAL, 1,
        EGL_NONE_VAL
    };
    ctx->surface_ = dispatcher.eglCreatePbufferSurface(dpy, config, pbuffer_attribs);

    // Test activation & load OpenGL function pointers
    EGLSurface test_surface = (ctx->surface_ != EGL_NO_SURFACE_PTR) ? ctx->surface_ : EGL_NO_SURFACE_PTR;
    if (dispatcher.eglMakeCurrent(dpy, test_surface, test_surface, egl_ctx) != EGL_TRUE_VAL) {
        ctx->reset();
        return MediaError::HardwareError;
    }

    // Load OpenGL function pointers while context is active
    if (!dispatcher.load_gl(dpy)) {
        ctx->reset();
        return MediaError::HardwareError;
    }

    // Cache GPU identification
    if (dispatcher.glGetString) {
        const auto* vendor = reinterpret_cast<const char*>(dispatcher.glGetString(0x1F00)); // GL_VENDOR
        const auto* renderer = reinterpret_cast<const char*>(dispatcher.glGetString(0x1F01)); // GL_RENDERER
        const auto* version = reinterpret_cast<const char*>(dispatcher.glGetString(0x1F02)); // GL_VERSION

        if (vendor) ctx->gl_vendor_ = vendor;
        if (renderer) ctx->gl_renderer_ = renderer;
        if (version) ctx->gl_version_ = version;
    }

    // Release context from current thread until explicit activation
    dispatcher.eglMakeCurrent(dpy, EGL_NO_SURFACE_PTR, EGL_NO_SURFACE_PTR, EGL_NO_CONTEXT_PTR);

    return ctx;
}

MediaResult<void> EglContext::make_current() const noexcept {
    if (!is_valid() || !dispatcher_->eglMakeCurrent) {
        return MediaError::InvalidState;
    }
    EGLSurface surf = (surface_ != EGL_NO_SURFACE_PTR) ? surface_ : EGL_NO_SURFACE_PTR;
    if (dispatcher_->eglMakeCurrent(display_, surf, surf, context_) != EGL_TRUE_VAL) {
        return MediaError::HardwareError;
    }
    return MediaResult<void>::ok();
}

void EglContext::release_current() const noexcept {
    if (display_ != EGL_NO_DISPLAY_PTR && dispatcher_->eglMakeCurrent) {
        dispatcher_->eglMakeCurrent(display_, EGL_NO_SURFACE_PTR, EGL_NO_SURFACE_PTR, EGL_NO_CONTEXT_PTR);
    }
}

bool EglContext::is_current() const noexcept {
    if (!is_valid() || !dispatcher_->eglGetCurrentContext) {
        return false;
    }
    return dispatcher_->eglGetCurrentContext() == context_;
}

} // namespace atum::gl
