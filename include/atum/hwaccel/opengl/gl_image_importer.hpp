#pragma once

#include <cstdint>
#include <memory>

#include "atum/surface/video_frame.hpp"
#include "atum/hwaccel/hwaccel.hpp"
#include "atum/hwaccel/opengl/gl_texture.hpp"
#include "atum/hwaccel/opengl/egl_context.hpp"

namespace atum::gl {

/// High-performance zero-copy DMA-BUF importer for OpenGL textures.
class GlImageImporter {
public:
    explicit GlImageImporter(const EglContext& context) noexcept;
    ~GlImageImporter() = default;

    // Non-copyable, movable
    GlImageImporter(const GlImageImporter&) = delete;
    GlImageImporter& operator=(const GlImageImporter&) = delete;
    GlImageImporter(GlImageImporter&& other) noexcept = default;
    GlImageImporter& operator=(GlImageImporter&& other) noexcept = default;

    /// Imports an external Linux DMA-BUF descriptor into an OpenGL texture with zero-copy.
    MediaResult<GlTexture> import_dma_buf(const HwBufferDescriptor& desc);

    /// Imports a hardware VideoFrame into an OpenGL texture with zero-copy.
    MediaResult<GlTexture> import_frame(const VideoFrame& frame);

    /// Creates an OpenGL texture from either a hardware DMA-BUF frame or host CPU memory.
    MediaResult<GlTexture> create_texture_from_frame(const VideoFrame& frame);

    [[nodiscard]] const EglContext& context() const noexcept { return *context_; }

private:
    const EglContext* context_{nullptr};
};

} // namespace atum::gl
