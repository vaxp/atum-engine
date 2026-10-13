#include "atum/hwaccel/opengl/gl_texture.hpp"

#include <cstring>
#include <vector>

namespace atum::gl {

GlTexture::GlTexture(const EglContext& context) noexcept
    : context_(&context) {}

GlTexture::~GlTexture() {
    reset();
}

GlTexture::GlTexture(GlTexture&& other) noexcept {
    *this = std::move(other);
}

GlTexture& GlTexture::operator=(GlTexture&& other) noexcept {
    if (this != &other) {
        reset();

        context_ = other.context_;
        texture_ = other.texture_;
        target_ = other.target_;
        egl_image_ = other.egl_image_;
        width_ = other.width_;
        height_ = other.height_;
        drm_fourcc_ = other.drm_fourcc_;

        other.context_ = nullptr;
        other.texture_ = 0;
        other.target_ = GL_TEXTURE_2D_VAL;
        other.egl_image_ = EGL_NO_IMAGE_KHR_PTR;
        other.width_ = 0;
        other.height_ = 0;
        other.drm_fourcc_ = 0;
    }
    return *this;
}

void GlTexture::reset() noexcept {
    if (!context_ || !context_->is_valid()) {
        texture_ = 0;
        egl_image_ = EGL_NO_IMAGE_KHR_PTR;
        return;
    }

    EglContext::Scope scope(*context_);
    const auto& disp = context_->dispatcher();

    if (texture_ != 0 && disp.glDeleteTextures) {
        disp.glDeleteTextures(1, &texture_);
        texture_ = 0;
    }

    if (egl_image_ != EGL_NO_IMAGE_KHR_PTR && disp.eglDestroyImageKHR) {
        disp.eglDestroyImageKHR(context_->display(), egl_image_);
        egl_image_ = EGL_NO_IMAGE_KHR_PTR;
    }

    width_ = 0;
    height_ = 0;
    drm_fourcc_ = 0;
}

MediaResult<VideoFrame> GlTexture::read_pixels_to_frame() const {
    if (!is_valid() || !context_ || !context_->is_valid()) {
        return MediaError::InvalidState;
    }

    EglContext::Scope scope(*context_);
    const auto& disp = context_->dispatcher();

    if (!disp.glGenFramebuffers || !disp.glBindFramebuffer || !disp.glFramebufferTexture2D || !disp.glReadPixels) {
        return MediaError::HardwareError;
    }

    GLuint fbo = 0;
    disp.glGenFramebuffers(1, &fbo);
    disp.glBindFramebuffer(GL_FRAMEBUFFER_VAL, fbo);
    disp.glFramebufferTexture2D(GL_FRAMEBUFFER_VAL, GL_COLOR_ATTACHMENT0_VAL, target_, texture_, 0);

    GLenum status = disp.glCheckFramebufferStatus(GL_FRAMEBUFFER_VAL);
    if (status != GL_FRAMEBUFFER_COMPLETE_VAL) {
        disp.glBindFramebuffer(GL_FRAMEBUFFER_VAL, 0);
        disp.glDeleteFramebuffers(1, &fbo);
        return MediaError::UnsupportedFormat;
    }

    std::vector<uint8_t> temp_buffer(static_cast<size_t>(width_) * height_ * 4);
    disp.glReadPixels(0, 0, width_, height_, GL_RGBA_VAL, GL_UNSIGNED_BYTE_VAL, temp_buffer.data());

    disp.glBindFramebuffer(GL_FRAMEBUFFER_VAL, 0);
    disp.glDeleteFramebuffers(1, &fbo);

    auto frame = VideoFrame::allocate(width_, height_, PixelFormat::RGBA32);
    if (!frame.is_valid()) {
        return MediaError::AllocationFailed;
    }

    // OpenGL texel row y matches VideoFrame row y
    const size_t row_bytes = static_cast<size_t>(width_) * 4;
    for (uint32_t y = 0; y < height_; ++y) {
        const uint8_t* src_row = temp_buffer.data() + static_cast<size_t>(y) * row_bytes;
        uint8_t* dst_row = frame.plane_mut(0) + y * frame.stride(0);
        std::memcpy(dst_row, src_row, row_bytes);
    }

    return frame;
}

} // namespace atum::gl
