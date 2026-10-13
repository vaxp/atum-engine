#include "atum/hwaccel/opengl/gl_device.hpp"

namespace atum::gl {

GlDevice::GlDevice() = default;

GlDevice::~GlDevice() = default;

GlDevice::GlDevice(GlDevice&& other) noexcept {
    *this = std::move(other);
}

GlDevice& GlDevice::operator=(GlDevice&& other) noexcept {
    if (this != &other) {
        dispatcher_ = std::move(other.dispatcher_);
        context_ = std::move(other.context_);
        importer_ = std::move(other.importer_);
        renderer_ = std::move(other.renderer_);
        name_ = std::move(other.name_);
    }
    return *this;
}

std::unique_ptr<GlDevice> GlDevice::create() {
    auto dev = std::unique_ptr<GlDevice>(new GlDevice());

    auto ctx_res = EglContext::create_headless(dev->dispatcher_);
    if (!ctx_res.is_ok()) {
        return nullptr;
    }

    dev->context_ = std::move(ctx_res.value());
    dev->importer_ = std::make_unique<GlImageImporter>(*dev->context_);
    dev->renderer_ = std::make_unique<GlRenderer>(*dev->context_);

    if (!dev->context_->gl_renderer().empty()) {
        dev->name_ = "OpenGL / EGL (" + std::string(dev->context_->gl_renderer()) + ")";
    } else {
        dev->name_ = "OpenGL / EGL";
    }

    return dev;
}

MediaResult<void> GlDevice::export_buffer(const VideoFrame& frame, HwBufferDescriptor& desc) {
    if (!frame.is_valid()) {
        return MediaError::InvalidData;
    }

    if (frame.is_hw_frame() && frame.dma_buf_fd() >= 0) {
        desc.fds[0] = frame.dma_buf_fd();
        desc.width = frame.width();
        desc.height = frame.height();
        desc.strides[0] = static_cast<uint32_t>(frame.stride(0));
        desc.offsets[0] = 0;
        desc.modifier = DRM_FORMAT_MOD_LINEAR_VAL;

        if (frame.pixel_format() == PixelFormat::NV12) {
            desc.drm_fourcc = DRM_FOURCC_NV12;
            desc.fds[1] = frame.dma_buf_fd();
            desc.offsets[1] = static_cast<uint32_t>(frame.height() * frame.stride(0));
            desc.strides[1] = static_cast<uint32_t>(frame.stride(1) > 0 ? frame.stride(1) : frame.stride(0));
        } else if (frame.pixel_format() == PixelFormat::P010) {
            desc.drm_fourcc = DRM_FOURCC_P010;
            desc.fds[1] = frame.dma_buf_fd();
            desc.offsets[1] = static_cast<uint32_t>(frame.height() * frame.stride(0));
            desc.strides[1] = static_cast<uint32_t>(frame.stride(1) > 0 ? frame.stride(1) : frame.stride(0));
        } else {
            desc.drm_fourcc = DRM_FOURCC_RGBA8888;
        }

        return MediaResult<void>::ok();
    }

    return MediaError::UnsupportedFormat;
}

MediaResult<VideoFrame> GlDevice::import_buffer(const HwBufferDescriptor& desc) {
    if (!importer_) {
        return MediaError::InvalidState;
    }

    auto tex_res = importer_->import_dma_buf(desc);
    if (!tex_res.is_ok()) {
        return tex_res.error();
    }

    return tex_res.value().read_pixels_to_frame();
}

} // namespace atum::gl
