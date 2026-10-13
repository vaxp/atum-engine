#include "atum/hwaccel/opengl/gl_image_importer.hpp"
#include "atum/surface/color_convert.hpp"

#include <vector>
#include <iostream>

namespace atum::gl {

GlImageImporter::GlImageImporter(const EglContext& context) noexcept
    : context_(&context) {}

MediaResult<GlTexture> GlImageImporter::import_dma_buf(const HwBufferDescriptor& desc) {
    if (!context_ || !context_->is_valid()) {
        return MediaError::InvalidState;
    }

    const auto& disp = context_->dispatcher();
    if (!disp.eglCreateImageKHR || !disp.glEGLImageTargetTexture2DOES) {
        return MediaError::HardwareError;
    }

    if (desc.fds[0] < 0 || desc.width == 0 || desc.height == 0) {
        return MediaError::InvalidData;
    }

    std::vector<EGLint> attribs;
    attribs.reserve(32);

    attribs.push_back(EGL_WIDTH_VAL);
    attribs.push_back(static_cast<EGLint>(desc.width));

    attribs.push_back(EGL_HEIGHT_VAL);
    attribs.push_back(static_cast<EGLint>(desc.height));

    attribs.push_back(EGL_LINUX_DRM_FOURCC_EXT_VAL);
    attribs.push_back(static_cast<EGLint>(desc.drm_fourcc));

    // Plane 0
    attribs.push_back(EGL_DMA_BUF_PLANE0_FD_EXT_VAL);
    attribs.push_back(desc.fds[0]);

    attribs.push_back(EGL_DMA_BUF_PLANE0_OFFSET_EXT_VAL);
    attribs.push_back(static_cast<EGLint>(desc.offsets[0]));

    attribs.push_back(EGL_DMA_BUF_PLANE0_PITCH_EXT_VAL);
    attribs.push_back(static_cast<EGLint>(desc.strides[0]));

    if (desc.modifier != DRM_FORMAT_MOD_INVALID_VAL && desc.modifier != 0) {
        attribs.push_back(EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT_VAL);
        attribs.push_back(static_cast<EGLint>(desc.modifier & 0xFFFFFFFF));
        attribs.push_back(EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT_VAL);
        attribs.push_back(static_cast<EGLint>((desc.modifier >> 32) & 0xFFFFFFFF));
    }

    // Plane 1 (e.g. UV in NV12 or P010)
    if (desc.fds[1] >= 0) {
        attribs.push_back(EGL_DMA_BUF_PLANE1_FD_EXT_VAL);
        attribs.push_back(desc.fds[1]);

        attribs.push_back(EGL_DMA_BUF_PLANE1_OFFSET_EXT_VAL);
        attribs.push_back(static_cast<EGLint>(desc.offsets[1]));

        attribs.push_back(EGL_DMA_BUF_PLANE1_PITCH_EXT_VAL);
        attribs.push_back(static_cast<EGLint>(desc.strides[1]));

        if (desc.modifier != DRM_FORMAT_MOD_INVALID_VAL && desc.modifier != 0) {
            attribs.push_back(EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT_VAL);
            attribs.push_back(static_cast<EGLint>(desc.modifier & 0xFFFFFFFF));
            attribs.push_back(EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT_VAL);
            attribs.push_back(static_cast<EGLint>((desc.modifier >> 32) & 0xFFFFFFFF));
        }
    }

    // Plane 2 (e.g. V in I420)
    if (desc.fds[2] >= 0) {
        attribs.push_back(EGL_DMA_BUF_PLANE2_FD_EXT_VAL);
        attribs.push_back(desc.fds[2]);

        attribs.push_back(EGL_DMA_BUF_PLANE2_OFFSET_EXT_VAL);
        attribs.push_back(static_cast<EGLint>(desc.offsets[2]));

        attribs.push_back(EGL_DMA_BUF_PLANE2_PITCH_EXT_VAL);
        attribs.push_back(static_cast<EGLint>(desc.strides[2]));

        if (desc.modifier != DRM_FORMAT_MOD_INVALID_VAL && desc.modifier != 0) {
            attribs.push_back(EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT_VAL);
            attribs.push_back(static_cast<EGLint>(desc.modifier & 0xFFFFFFFF));
            attribs.push_back(EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT_VAL);
            attribs.push_back(static_cast<EGLint>((desc.modifier >> 32) & 0xFFFFFFFF));
        }
    }

    attribs.push_back(EGL_NONE_VAL);

    EglContext::Scope scope(*context_);

    EGLImageKHR image = disp.eglCreateImageKHR(
        context_->display(),
        EGL_NO_CONTEXT_PTR,
        EGL_LINUX_DMA_BUF_EXT_VAL,
        nullptr,
        attribs.data()
    );

    if (image == EGL_NO_IMAGE_KHR_PTR) {
        return MediaError::UnsupportedFormat;
    }

    GLuint texture_id = 0;
    disp.glGenTextures(1, &texture_id);

    GLenum target = GL_TEXTURE_2D_VAL;
    disp.glBindTexture(target, texture_id);
    disp.glEGLImageTargetTexture2DOES(target, image);

    GLenum gl_err = disp.glGetError ? disp.glGetError() : 0;
    if (gl_err != GL_NO_ERROR_VAL) {
        // Fallback to GL_TEXTURE_EXTERNAL_OES target
        target = GL_TEXTURE_EXTERNAL_OES_VAL;
        disp.glBindTexture(target, texture_id);
        disp.glEGLImageTargetTexture2DOES(target, image);
    }

    disp.glTexParameteri(target, GL_TEXTURE_MIN_FILTER_VAL, GL_LINEAR_VAL);
    disp.glTexParameteri(target, GL_TEXTURE_MAG_FILTER_VAL, GL_LINEAR_VAL);
    disp.glTexParameteri(target, GL_TEXTURE_WRAP_S_VAL, GL_CLAMP_TO_EDGE_VAL);
    disp.glTexParameteri(target, GL_TEXTURE_WRAP_T_VAL, GL_CLAMP_TO_EDGE_VAL);

    disp.glBindTexture(target, 0);

    GlTexture texture(*context_);
    texture.texture_ = texture_id;
    texture.target_ = target;
    texture.egl_image_ = image;
    texture.width_ = desc.width;
    texture.height_ = desc.height;
    texture.drm_fourcc_ = desc.drm_fourcc;

    return texture;
}

MediaResult<GlTexture> GlImageImporter::import_frame(const VideoFrame& frame) {
    if (!frame.is_valid()) {
        return MediaError::InvalidData;
    }

    if (!frame.is_hw_frame() || frame.dma_buf_fd() < 0) {
        return MediaError::InvalidData;
    }

    HwBufferDescriptor desc{};
    desc.fds[0] = frame.dma_buf_fd();
    desc.width = frame.width();
    desc.height = frame.height();
    desc.strides[0] = static_cast<uint32_t>(frame.stride(0));
    desc.offsets[0] = 0;
    desc.modifier = DRM_FORMAT_MOD_LINEAR_VAL;

    switch (frame.pixel_format()) {
        case PixelFormat::NV12:
            desc.drm_fourcc = DRM_FOURCC_NV12;
            desc.fds[1] = frame.dma_buf_fd();
            desc.offsets[1] = static_cast<uint32_t>(frame.height() * frame.stride(0));
            desc.strides[1] = static_cast<uint32_t>(frame.stride(1) > 0 ? frame.stride(1) : frame.stride(0));
            break;
        case PixelFormat::P010:
            desc.drm_fourcc = DRM_FOURCC_P010;
            desc.fds[1] = frame.dma_buf_fd();
            desc.offsets[1] = static_cast<uint32_t>(frame.height() * frame.stride(0));
            desc.strides[1] = static_cast<uint32_t>(frame.stride(1) > 0 ? frame.stride(1) : frame.stride(0));
            break;
        case PixelFormat::RGBA32:
            desc.drm_fourcc = DRM_FOURCC_RGBA8888;
            break;
        case PixelFormat::BGRA32:
            desc.drm_fourcc = DRM_FOURCC_ARGB8888;
            break;
        default:
            desc.drm_fourcc = DRM_FOURCC_NV12;
            break;
    }

    return import_dma_buf(desc);
}

MediaResult<GlTexture> GlImageImporter::create_texture_from_frame(const VideoFrame& frame) {
    if (!frame.is_valid()) {
        return MediaError::InvalidData;
    }

    // Direct hardware zero-copy path
    if (frame.is_hw_frame() && frame.dma_buf_fd() >= 0) {
        auto hw_res = import_frame(frame);
        if (hw_res.is_ok()) {
            return hw_res;
        }
    }

    // Host CPU memory upload path
    if (!context_ || !context_->is_valid()) {
        return MediaError::InvalidState;
    }

    EglContext::Scope scope(*context_);
    const auto& disp = context_->dispatcher();

    GLuint texture_id = 0;
    disp.glGenTextures(1, &texture_id);
    disp.glBindTexture(GL_TEXTURE_2D_VAL, texture_id);

    std::vector<uint8_t> converted_rgba;
    const uint8_t* upload_ptr = nullptr;
    GLenum gl_fmt = GL_RGBA_VAL;

    if (frame.pixel_format() == PixelFormat::RGBA32) {
        upload_ptr = frame.plane(0);
        gl_fmt = GL_RGBA_VAL;
    } else if (frame.pixel_format() == PixelFormat::RGB24) {
        upload_ptr = frame.plane(0);
        gl_fmt = GL_RGB_VAL;
    } else if (frame.pixel_format() == PixelFormat::NV12) {
        converted_rgba.resize(static_cast<size_t>(frame.width()) * frame.height() * 4);
        ColorConverter::nv12_to_rgba(
            frame.plane(0), frame.stride(0),
            frame.plane(1), frame.stride(1),
            converted_rgba.data(), frame.width() * 4,
            frame.width(), frame.height(),
            frame.color_space(), frame.color_range()
        );
        upload_ptr = converted_rgba.data();
        gl_fmt = GL_RGBA_VAL;
    } else if (frame.pixel_format() == PixelFormat::YUV420P) {
        converted_rgba.resize(static_cast<size_t>(frame.width()) * frame.height() * 4);
        ColorConverter::yuv420p_to_rgba(
            frame.plane(0), frame.stride(0),
            frame.plane(1), frame.stride(1),
            frame.plane(2), frame.stride(2),
            converted_rgba.data(), frame.width() * 4,
            frame.width(), frame.height(),
            frame.color_space(), frame.color_range()
        );
        upload_ptr = converted_rgba.data();
        gl_fmt = GL_RGBA_VAL;
    } else {
        return MediaError::UnsupportedFormat;
    }

    disp.glTexImage2D(
        GL_TEXTURE_2D_VAL,
        0,
        static_cast<GLint>(gl_fmt),
        static_cast<GLsizei>(frame.width()),
        static_cast<GLsizei>(frame.height()),
        0,
        gl_fmt,
        GL_UNSIGNED_BYTE_VAL,
        upload_ptr
    );

    disp.glTexParameteri(GL_TEXTURE_2D_VAL, GL_TEXTURE_MIN_FILTER_VAL, GL_LINEAR_VAL);
    disp.glTexParameteri(GL_TEXTURE_2D_VAL, GL_TEXTURE_MAG_FILTER_VAL, GL_LINEAR_VAL);
    disp.glTexParameteri(GL_TEXTURE_2D_VAL, GL_TEXTURE_WRAP_S_VAL, GL_CLAMP_TO_EDGE_VAL);
    disp.glTexParameteri(GL_TEXTURE_2D_VAL, GL_TEXTURE_WRAP_T_VAL, GL_CLAMP_TO_EDGE_VAL);

    disp.glBindTexture(GL_TEXTURE_2D_VAL, 0);

    GlTexture texture(*context_);
    texture.texture_ = texture_id;
    texture.target_ = GL_TEXTURE_2D_VAL;
    texture.width_ = frame.width();
    texture.height_ = frame.height();

    return texture;
}

} // namespace atum::gl
