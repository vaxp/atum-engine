#pragma once

#include <cstdint>
#include <memory>

#include "atum/core/media_types.hpp"
#include "atum/surface/video_frame.hpp"
#include "atum/hwaccel/opengl/gl_types.hpp"
#include "atum/hwaccel/opengl/gl_texture.hpp"
#include "atum/hwaccel/opengl/egl_context.hpp"

namespace atum::gl {

/// High-performance fullscreen quad presentation and shader rendering engine.
class GlRenderer {
public:
    explicit GlRenderer(const EglContext& context) noexcept;
    ~GlRenderer();

    // Non-copyable, movable
    GlRenderer(const GlRenderer&) = delete;
    GlRenderer& operator=(const GlRenderer&) = delete;
    GlRenderer(GlRenderer&& other) noexcept;
    GlRenderer& operator=(GlRenderer&& other) noexcept;

    /// Initializes shaders, VAO, and VBO geometry.
    MediaResult<void> init();

    /// Renders an imported GlTexture into a target FBO with custom viewport dimensions.
    MediaResult<void> render_to_fbo(
        const GlTexture& src,
        GLuint dst_fbo,
        uint32_t dst_width,
        uint32_t dst_height,
        ColorSpace color_space = ColorSpace::BT709,
        ColorRange color_range = ColorRange::Studio
    );

    /// Renders an imported GlTexture into an owned RGBA destination texture.
    MediaResult<GlTexture> render_to_texture(
        const GlTexture& src,
        uint32_t dst_width,
        uint32_t dst_height,
        ColorSpace color_space = ColorSpace::BT709,
        ColorRange color_range = ColorRange::Studio
    );

    /// Releases all shader and pipeline resources.
    void reset() noexcept;

    [[nodiscard]] bool is_initialized() const noexcept { return program_ != 0; }

private:
    const EglContext* context_{nullptr};
    GLuint program_{0};
    GLuint vao_{0};
    GLuint vbo_{0};

    GLint u_texture_loc_{-1};
    GLint u_color_conv_loc_{-1};
    GLint u_color_range_loc_{-1};
};

} // namespace atum::gl
