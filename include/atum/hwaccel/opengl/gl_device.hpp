#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <memory>

#include "atum/hwaccel/hwaccel.hpp"
#include "atum/hwaccel/opengl/gl_dispatcher.hpp"
#include "atum/hwaccel/opengl/egl_context.hpp"
#include "atum/hwaccel/opengl/gl_image_importer.hpp"
#include "atum/hwaccel/opengl/gl_renderer.hpp"

namespace atum::gl {

/// Sovereign OpenGL / EGL Hardware Acceleration Device.
class GlDevice : public HwDevice {
public:
    GlDevice();
    ~GlDevice() override;

    // Non-copyable, movable
    GlDevice(const GlDevice&) = delete;
    GlDevice& operator=(const GlDevice&) = delete;
    GlDevice(GlDevice&& other) noexcept;
    GlDevice& operator=(GlDevice&& other) noexcept;

    [[nodiscard]] HwDeviceType type() const noexcept override {
        return HwDeviceType::OpenGl;
    }

    [[nodiscard]] std::string_view name() const noexcept override {
        return name_;
    }

    [[nodiscard]] bool is_available() const noexcept override {
        return context_ && context_->is_valid();
    }

    MediaResult<void> export_buffer(const VideoFrame& frame, HwBufferDescriptor& desc) override;
    MediaResult<VideoFrame> import_buffer(const HwBufferDescriptor& desc) override;

    /// Factory: discovers and initializes headless OpenGL / EGL device.
    static std::unique_ptr<GlDevice> create();

    // --- Subsystem Accessors ---
    [[nodiscard]] const GlDispatcher& dispatcher() const noexcept { return dispatcher_; }
    [[nodiscard]] GlDispatcher& dispatcher() noexcept { return dispatcher_; }

    [[nodiscard]] const EglContext& context() const noexcept { return *context_; }
    [[nodiscard]] EglContext& context() noexcept { return *context_; }

    [[nodiscard]] GlImageImporter& importer() noexcept { return *importer_; }
    [[nodiscard]] const GlImageImporter& importer() const noexcept { return *importer_; }

    [[nodiscard]] GlRenderer& renderer() noexcept { return *renderer_; }
    [[nodiscard]] const GlRenderer& renderer() const noexcept { return *renderer_; }

private:
    GlDispatcher dispatcher_;
    std::unique_ptr<EglContext> context_;
    std::unique_ptr<GlImageImporter> importer_;
    std::unique_ptr<GlRenderer> renderer_;
    std::string name_{"OpenGL / EGL"};
};

} // namespace atum::gl
