#pragma once

#include <cstdint>
#include <memory>
#include <utility>

#include "atum/surface/video_frame.hpp"
#include "atum/hwaccel/opengl/gl_types.hpp"
#include "atum/hwaccel/opengl/egl_context.hpp"

namespace atum::gl {

class GlImageImporter;

/// RAII wrapper for an OpenGL Texture and optional underlying EGLImageKHR.
class GlTexture {
public:
    GlTexture() noexcept = default;
    explicit GlTexture(const EglContext& context) noexcept;
    ~GlTexture();

    // Non-copyable, movable
    GlTexture(const GlTexture&) = delete;
    GlTexture& operator=(const GlTexture&) = delete;
    GlTexture(GlTexture&& other) noexcept;
    GlTexture& operator=(GlTexture&& other) noexcept;

    [[nodiscard]] GLuint id() const noexcept { return texture_; }
    [[nodiscard]] GLenum target() const noexcept { return target_; }
    [[nodiscard]] EGLImageKHR egl_image() const noexcept { return egl_image_; }
    [[nodiscard]] uint32_t width() const noexcept { return width_; }
    [[nodiscard]] uint32_t height() const noexcept { return height_; }
    [[nodiscard]] uint32_t drm_fourcc() const noexcept { return drm_fourcc_; }
    [[nodiscard]] bool is_valid() const noexcept { return texture_ != 0; }

    /// Reads back texture pixels into CPU host memory VideoFrame (RGBA format).
    /// Performs bottom-to-top inversion so rows are top-down.
    MediaResult<VideoFrame> read_pixels_to_frame() const;

    /// Releases all native OpenGL and EGL texture resources.
    void reset() noexcept;

private:
    friend class GlImageImporter;
    friend class GlRenderer;

    const EglContext* context_{nullptr};
    GLuint texture_{0};
    GLenum target_{GL_TEXTURE_2D_VAL};
    EGLImageKHR egl_image_{EGL_NO_IMAGE_KHR_PTR};
    uint32_t width_{0};
    uint32_t height_{0};
    uint32_t drm_fourcc_{0};
};

} // namespace atum::gl
