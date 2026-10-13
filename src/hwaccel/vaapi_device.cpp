#include "atum/hwaccel/vaapi_device.hpp"
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>
#endif
#include <cstring>
#include <iostream>
#include <algorithm>

#if __has_include(<va/va.h>) && __has_include(<va/va_drm.h>)
#include <va/va.h>
#include <va/va_drm.h>
#if __has_include(<va/va_drmcommon.h>)
#include <va/va_drmcommon.h>
#endif
#define NISABA_HAS_VAAPI_HEADERS 1
#endif

namespace atum {

#if defined(NISABA_HAS_VAAPI_HEADERS)
namespace {

// Function pointer signatures for dynamic libva loading
typedef VADisplay (*FnVaGetDisplayDRM)(int fd);
typedef VAStatus (*FnVaInitialize)(VADisplay dpy, int* major_version, int* minor_version);
typedef VAStatus (*FnVaTerminate)(VADisplay dpy);
typedef const char* (*FnVaErrorStr)(VAStatus error_status);
typedef int (*FnVaMaxNumProfiles)(VADisplay dpy);
typedef VAStatus (*FnVaQueryConfigProfiles)(VADisplay dpy, VAProfile* profile_list, int* num_profiles);
typedef VAStatus (*FnVaCreateConfig)(VADisplay dpy, VAProfile profile, VAEntrypoint entrypoint, VAConfigAttrib* attrib_list, int num_attribs, VAConfigID* config_id);
typedef VAStatus (*FnVaDestroyConfig)(VADisplay dpy, VAConfigID config_id);
typedef VAStatus (*FnVaCreateSurfaces)(VADisplay dpy, unsigned int format, unsigned int width, unsigned int height, VASurfaceID* surfaces, unsigned int num_surfaces, VASurfaceAttrib* attrib_list, unsigned int num_attribs);
typedef VAStatus (*FnVaDestroySurfaces)(VADisplay dpy, VASurfaceID* surface_list, int num_surfaces);
typedef VAStatus (*FnVaCreateContext)(VADisplay dpy, VAConfigID config_id, int picture_width, int picture_height, int flag, VASurfaceID* render_targets, int num_render_targets, VAContextID* context);
typedef VAStatus (*FnVaDestroyContext)(VADisplay dpy, VAContextID context);
typedef VAStatus (*FnVaCreateBuffer)(VADisplay dpy, VAContextID context, VABufferType type, unsigned int size, unsigned int num_elements, void* data, VABufferID* buf_id);
typedef VAStatus (*FnVaDestroyBuffer)(VADisplay dpy, VABufferID buffer_id);
typedef VAStatus (*FnVaBeginPicture)(VADisplay dpy, VAContextID context, VASurfaceID render_target);
typedef VAStatus (*FnVaRenderPicture)(VADisplay dpy, VAContextID context, VABufferID* buffers, int num_buffers);
typedef VAStatus (*FnVaEndPicture)(VADisplay dpy, VAContextID context);
typedef VAStatus (*FnVaSyncSurface)(VADisplay dpy, VASurfaceID render_target);
typedef VAStatus (*FnVaDeriveImage)(VADisplay dpy, VASurfaceID surface, VAImage* image);
typedef VAStatus (*FnVaDestroyImage)(VADisplay dpy, VAImageID image);
typedef VAStatus (*FnVaCreateImage)(VADisplay dpy, VAImageFormat* format, int width, int height, VAImage* image);
typedef VAStatus (*FnVaPutImage)(VADisplay dpy, VASurfaceID surface, VAImageID image, int src_x, int src_y, unsigned int src_width, unsigned int src_height, int dest_x, int dest_y, unsigned int dest_width, unsigned int dest_height);
typedef VAStatus (*FnVaMapBuffer)(VADisplay dpy, VABufferID buf_id, void** pbuf);
typedef VAStatus (*FnVaUnmapBuffer)(VADisplay dpy, VABufferID buf_id);
typedef VAStatus (*FnVaExportSurfaceHandle)(VADisplay dpy, VASurfaceID surface_id, uint32_t mem_type, uint32_t flags, void* descriptor);

struct VaSymbols {
    FnVaGetDisplayDRM vaGetDisplayDRM{nullptr};
    FnVaInitialize vaInitialize{nullptr};
    FnVaTerminate vaTerminate{nullptr};
    FnVaErrorStr vaErrorStr{nullptr};
    FnVaMaxNumProfiles vaMaxNumProfiles{nullptr};
    FnVaQueryConfigProfiles vaQueryConfigProfiles{nullptr};
    FnVaCreateConfig vaCreateConfig{nullptr};
    FnVaDestroyConfig vaDestroyConfig{nullptr};
    FnVaCreateSurfaces vaCreateSurfaces{nullptr};
    FnVaDestroySurfaces vaDestroySurfaces{nullptr};
    FnVaCreateContext vaCreateContext{nullptr};
    FnVaDestroyContext vaDestroyContext{nullptr};
    FnVaCreateBuffer vaCreateBuffer{nullptr};
    FnVaDestroyBuffer vaDestroyBuffer{nullptr};
    FnVaBeginPicture vaBeginPicture{nullptr};
    FnVaRenderPicture vaRenderPicture{nullptr};
    FnVaEndPicture vaEndPicture{nullptr};
    FnVaSyncSurface vaSyncSurface{nullptr};
    FnVaDeriveImage vaDeriveImage{nullptr};
    FnVaDestroyImage vaDestroyImage{nullptr};
    FnVaCreateImage vaCreateImage{nullptr};
    FnVaPutImage vaPutImage{nullptr};
    FnVaMapBuffer vaMapBuffer{nullptr};
    FnVaUnmapBuffer vaUnmapBuffer{nullptr};
    FnVaExportSurfaceHandle vaExportSurfaceHandle{nullptr};

    bool load(void* libva, void* libva_drm) noexcept {
        if (!libva || !libva_drm) return false;

        vaGetDisplayDRM = reinterpret_cast<FnVaGetDisplayDRM>(dlsym(libva_drm, "vaGetDisplayDRM"));
        vaInitialize = reinterpret_cast<FnVaInitialize>(dlsym(libva, "vaInitialize"));
        vaTerminate = reinterpret_cast<FnVaTerminate>(dlsym(libva, "vaTerminate"));
        vaErrorStr = reinterpret_cast<FnVaErrorStr>(dlsym(libva, "vaErrorStr"));
        vaMaxNumProfiles = reinterpret_cast<FnVaMaxNumProfiles>(dlsym(libva, "vaMaxNumProfiles"));
        vaQueryConfigProfiles = reinterpret_cast<FnVaQueryConfigProfiles>(dlsym(libva, "vaQueryConfigProfiles"));
        vaCreateConfig = reinterpret_cast<FnVaCreateConfig>(dlsym(libva, "vaCreateConfig"));
        vaDestroyConfig = reinterpret_cast<FnVaDestroyConfig>(dlsym(libva, "vaDestroyConfig"));
        vaCreateSurfaces = reinterpret_cast<FnVaCreateSurfaces>(dlsym(libva, "vaCreateSurfaces"));
        vaDestroySurfaces = reinterpret_cast<FnVaDestroySurfaces>(dlsym(libva, "vaDestroySurfaces"));
        vaCreateContext = reinterpret_cast<FnVaCreateContext>(dlsym(libva, "vaCreateContext"));
        vaDestroyContext = reinterpret_cast<FnVaDestroyContext>(dlsym(libva, "vaDestroyContext"));
        vaCreateBuffer = reinterpret_cast<FnVaCreateBuffer>(dlsym(libva, "vaCreateBuffer"));
        vaDestroyBuffer = reinterpret_cast<FnVaDestroyBuffer>(dlsym(libva, "vaDestroyBuffer"));
        vaBeginPicture = reinterpret_cast<FnVaBeginPicture>(dlsym(libva, "vaBeginPicture"));
        vaRenderPicture = reinterpret_cast<FnVaRenderPicture>(dlsym(libva, "vaRenderPicture"));
        vaEndPicture = reinterpret_cast<FnVaEndPicture>(dlsym(libva, "vaEndPicture"));
        vaSyncSurface = reinterpret_cast<FnVaSyncSurface>(dlsym(libva, "vaSyncSurface"));
        vaDeriveImage = reinterpret_cast<FnVaDeriveImage>(dlsym(libva, "vaDeriveImage"));
        vaDestroyImage = reinterpret_cast<FnVaDestroyImage>(dlsym(libva, "vaDestroyImage"));
        vaCreateImage = reinterpret_cast<FnVaCreateImage>(dlsym(libva, "vaCreateImage"));
        vaPutImage = reinterpret_cast<FnVaPutImage>(dlsym(libva, "vaPutImage"));
        vaMapBuffer = reinterpret_cast<FnVaMapBuffer>(dlsym(libva, "vaMapBuffer"));
        vaUnmapBuffer = reinterpret_cast<FnVaUnmapBuffer>(dlsym(libva, "vaUnmapBuffer"));
        vaExportSurfaceHandle = reinterpret_cast<FnVaExportSurfaceHandle>(dlsym(libva, "vaExportSurfaceHandle"));

        return vaGetDisplayDRM && vaInitialize && vaTerminate && vaErrorStr &&
               vaMaxNumProfiles && vaQueryConfigProfiles;
    }
};

static VaSymbols g_va_syms{};

} // anonymous namespace
#endif

VaapiDevice::VaapiDevice() {
    init_internal();
}

VaapiDevice::~VaapiDevice() {
    close_internal();
}

void VaapiDevice::init_internal() noexcept {
#if defined(NISABA_HAS_VAAPI_HEADERS)
    // 1. Attempt to dynamic-load libva and libva-drm
    libva_handle_ = dlopen("libva.so.2", RTLD_NOW);
    if (!libva_handle_) {
        libva_handle_ = dlopen("libva.so", RTLD_NOW);
    }
    libva_drm_handle_ = dlopen("libva-drm.so.2", RTLD_NOW);
    if (!libva_drm_handle_) {
        libva_drm_handle_ = dlopen("libva-drm.so", RTLD_NOW);
    }

    if (!libva_handle_ || !libva_drm_handle_) {
        return;
    }

    if (!g_va_syms.load(libva_handle_, libva_drm_handle_)) {
        return;
    }

    // 2. Open Linux DRM render node (/dev/dri/renderD128 .. renderD136)
    const char* render_nodes[] = {
        "/dev/dri/renderD128",
        "/dev/dri/renderD129",
        "/dev/dri/renderD130",
        "/dev/dri/card0"
    };

    for (const char* node : render_nodes) {
        drm_fd_ = open(node, O_RDWR | O_CLOEXEC);
        if (drm_fd_ >= 0) {
            break;
        }
    }

    if (drm_fd_ < 0) {
        return;
    }

    // 3. Initialize VADisplay
    VADisplay dpy = g_va_syms.vaGetDisplayDRM(drm_fd_);
    if (!dpy) {
        close(drm_fd_);
        drm_fd_ = -1;
        return;
    }

    int major = 0, minor = 0;
    VAStatus status = g_va_syms.vaInitialize(dpy, &major, &minor);
    if (status != VA_STATUS_SUCCESS) {
        close(drm_fd_);
        drm_fd_ = -1;
        return;
    }

    va_display_ = dpy;
    is_initialized_ = true;
#endif
}

void VaapiDevice::close_internal() noexcept {
#if defined(NISABA_HAS_VAAPI_HEADERS)
    if (is_initialized_ && va_display_ && g_va_syms.vaTerminate) {
        g_va_syms.vaTerminate(reinterpret_cast<VADisplay>(va_display_));
        va_display_ = nullptr;
    }
    if (drm_fd_ >= 0) {
        close(drm_fd_);
        drm_fd_ = -1;
    }
    if (libva_drm_handle_) {
        dlclose(libva_drm_handle_);
        libva_drm_handle_ = nullptr;
    }
    if (libva_handle_) {
        dlclose(libva_handle_);
        libva_handle_ = nullptr;
    }
    is_initialized_ = false;
#endif
}

bool VaapiDevice::is_available() const noexcept {
    return is_initialized_ && va_display_ != nullptr;
}

bool VaapiDevice::supports_codec(CodecId codec) const noexcept {
#if defined(NISABA_HAS_VAAPI_HEADERS)
    if (!is_available()) return false;

    VADisplay dpy = reinterpret_cast<VADisplay>(va_display_);
    if (!g_va_syms.vaMaxNumProfiles || !g_va_syms.vaQueryConfigProfiles) return false;
    int max_profiles = g_va_syms.vaMaxNumProfiles(dpy);
    if (max_profiles <= 0) {
        return false;
    }

    std::vector<VAProfile> profiles(static_cast<size_t>(max_profiles));
    int actual_num = 0;
    if (g_va_syms.vaQueryConfigProfiles(dpy, profiles.data(), &actual_num) != VA_STATUS_SUCCESS) {
        return false;
    }

    for (int i = 0; i < actual_num; ++i) {
        VAProfile p = profiles[static_cast<size_t>(i)];
        if (codec == CodecId::H264) {
            if (p == VAProfileH264Main ||
                p == VAProfileH264High || p == VAProfileH264ConstrainedBaseline) {
                return true;
            }
        } else if (codec == CodecId::H265 || codec == CodecId::HEVC) {
            if (p == VAProfileHEVCMain || p == VAProfileHEVCMain10) {
                return true;
            }
        } else if (codec == CodecId::VP9) {
            if (p == VAProfileVP9Profile0 || p == VAProfileVP9Profile2) {
                return true;
            }
        } else if (codec == CodecId::AV1) {
            if (p == VAProfileAV1Profile0) {
                return true;
            }
        }
    }
#else
    (void)codec;
#endif
    return false;
}

MediaResult<uint32_t> VaapiDevice::create_config(CodecId codec, bool is_encoder, uint32_t rate_control) {
#if defined(NISABA_HAS_VAAPI_HEADERS)
    if (!is_available() || !g_va_syms.vaCreateConfig) return MediaError::HardwareError;

    VADisplay dpy = reinterpret_cast<VADisplay>(va_display_);
    VAProfile profile = VAProfileNone;

    if (codec == CodecId::H264) profile = VAProfileH264High;
    else if (codec == CodecId::H265 || codec == CodecId::HEVC) profile = VAProfileHEVCMain;
    else if (codec == CodecId::VP9) profile = VAProfileVP9Profile0;
    else if (codec == CodecId::AV1) profile = VAProfileAV1Profile0;
    else return MediaError::UnsupportedCodec;

    VAEntrypoint entrypoint = is_encoder ? VAEntrypointEncSlice : VAEntrypointVLD;
    VAConfigAttrib attribs[2];
    int num_attribs = 0;
    if (is_encoder) {
        attribs[0].type = VAConfigAttribRateControl;
        attribs[0].value = (rate_control != 0) ? rate_control : VA_RC_CQP;
        num_attribs = 1;
    }

    VAConfigID cfg = VA_INVALID_ID;
    VAStatus st = g_va_syms.vaCreateConfig(dpy, profile, entrypoint, num_attribs > 0 ? attribs : nullptr, num_attribs, &cfg);
    if (st != VA_STATUS_SUCCESS) {
        // Fallback for H.264
        if (codec == CodecId::H264) {
            st = g_va_syms.vaCreateConfig(dpy, VAProfileH264Main, entrypoint, num_attribs > 0 ? attribs : nullptr, num_attribs, &cfg);
            if (st != VA_STATUS_SUCCESS && is_encoder) {
                st = g_va_syms.vaCreateConfig(dpy, VAProfileH264ConstrainedBaseline, entrypoint, num_attribs > 0 ? attribs : nullptr, num_attribs, &cfg);
            }
        }
    }

    if (st != VA_STATUS_SUCCESS || cfg == VA_INVALID_ID) {
        return MediaError::HardwareError;
    }
    return static_cast<uint32_t>(cfg);
#else
    (void)codec; (void)is_encoder; (void)rate_control;
    return MediaError::NotImplemented;
#endif
}

MediaResult<void> VaapiDevice::destroy_config(uint32_t config_id) {
#if defined(NISABA_HAS_VAAPI_HEADERS)
    if (!is_available() || !g_va_syms.vaDestroyConfig) return MediaError::HardwareError;
    VADisplay dpy = reinterpret_cast<VADisplay>(va_display_);
    VAStatus st = g_va_syms.vaDestroyConfig(dpy, static_cast<VAConfigID>(config_id));
    if (st != VA_STATUS_SUCCESS) return MediaError::HardwareError;
    return MediaResult<void>::ok();
#else
    (void)config_id;
    return MediaError::NotImplemented;
#endif
}

MediaResult<std::vector<uint32_t>> VaapiDevice::create_surfaces(uint32_t width, uint32_t height, PixelFormat format, uint32_t count) {
#if defined(NISABA_HAS_VAAPI_HEADERS)
    if (!is_available() || !g_va_syms.vaCreateSurfaces || count == 0) return MediaError::HardwareError;

    VADisplay dpy = reinterpret_cast<VADisplay>(va_display_);
    unsigned int rt_format = VA_RT_FORMAT_YUV420;
    if (format == PixelFormat::P010 || format == PixelFormat::YUV420P10) {
        rt_format = VA_RT_FORMAT_YUV420_10;
    }

    std::vector<VASurfaceID> va_surfaces(count, VA_INVALID_ID);
    VAStatus st = g_va_syms.vaCreateSurfaces(
        dpy, rt_format, width, height,
        va_surfaces.data(), count,
        nullptr, 0
    );

    if (st != VA_STATUS_SUCCESS) {
        return MediaError::HardwareError;
    }

    std::vector<uint32_t> result;
    result.reserve(count);
    for (auto s : va_surfaces) {
        result.push_back(static_cast<uint32_t>(s));
    }
    return result;
#else
    (void)width; (void)height; (void)format; (void)count;
    return MediaError::NotImplemented;
#endif
}

MediaResult<void> VaapiDevice::destroy_surfaces(const std::vector<uint32_t>& surfaces) {
#if defined(NISABA_HAS_VAAPI_HEADERS)
    if (!is_available() || !g_va_syms.vaDestroySurfaces || surfaces.empty()) return MediaError::HardwareError;
    VADisplay dpy = reinterpret_cast<VADisplay>(va_display_);

    std::vector<VASurfaceID> va_surfaces;
    va_surfaces.reserve(surfaces.size());
    for (auto s : surfaces) va_surfaces.push_back(static_cast<VASurfaceID>(s));

    VAStatus st = g_va_syms.vaDestroySurfaces(dpy, va_surfaces.data(), static_cast<int>(va_surfaces.size()));
    if (st != VA_STATUS_SUCCESS) return MediaError::HardwareError;
    return MediaResult<void>::ok();
#else
    (void)surfaces;
    return MediaError::NotImplemented;
#endif
}

MediaResult<uint32_t> VaapiDevice::create_context(uint32_t config_id, uint32_t width, uint32_t height, const std::vector<uint32_t>& surfaces) {
#if defined(NISABA_HAS_VAAPI_HEADERS)
    if (!is_available() || !g_va_syms.vaCreateContext) return MediaError::HardwareError;
    VADisplay dpy = reinterpret_cast<VADisplay>(va_display_);

    std::vector<VASurfaceID> va_surfaces;
    va_surfaces.reserve(surfaces.size());
    for (auto s : surfaces) va_surfaces.push_back(static_cast<VASurfaceID>(s));

    VAContextID ctx = VA_INVALID_ID;
    VAStatus st = g_va_syms.vaCreateContext(
        dpy,
        static_cast<VAConfigID>(config_id),
        static_cast<int>(width),
        static_cast<int>(height),
        VA_PROGRESSIVE,
        va_surfaces.empty() ? nullptr : va_surfaces.data(),
        static_cast<int>(va_surfaces.size()),
        &ctx
    );

    if (st != VA_STATUS_SUCCESS || ctx == VA_INVALID_ID) {
        return MediaError::HardwareError;
    }
    return static_cast<uint32_t>(ctx);
#else
    (void)config_id; (void)width; (void)height; (void)surfaces;
    return MediaError::NotImplemented;
#endif
}

MediaResult<void> VaapiDevice::destroy_context(uint32_t context_id) {
#if defined(NISABA_HAS_VAAPI_HEADERS)
    if (!is_available() || !g_va_syms.vaDestroyContext) return MediaError::HardwareError;
    VADisplay dpy = reinterpret_cast<VADisplay>(va_display_);
    VAStatus st = g_va_syms.vaDestroyContext(dpy, static_cast<VAContextID>(context_id));
    if (st != VA_STATUS_SUCCESS) return MediaError::HardwareError;
    return MediaResult<void>::ok();
#else
    (void)context_id;
    return MediaError::NotImplemented;
#endif
}

MediaResult<uint32_t> VaapiDevice::create_buffer(uint32_t context_id, int buffer_type, uint32_t size, uint32_t num_elements, const void* data) {
#if defined(NISABA_HAS_VAAPI_HEADERS)
    if (!is_available() || !g_va_syms.vaCreateBuffer) return MediaError::HardwareError;
    VADisplay dpy = reinterpret_cast<VADisplay>(va_display_);

    VABufferID buf_id = VA_INVALID_ID;
    VAStatus st = g_va_syms.vaCreateBuffer(
        dpy,
        static_cast<VAContextID>(context_id),
        static_cast<VABufferType>(buffer_type),
        size,
        num_elements,
        const_cast<void*>(data),
        &buf_id
    );

    if (st != VA_STATUS_SUCCESS || buf_id == VA_INVALID_ID) {
        return MediaError::HardwareError;
    }
    return static_cast<uint32_t>(buf_id);
#else
    (void)context_id; (void)buffer_type; (void)size; (void)num_elements; (void)data;
    return MediaError::NotImplemented;
#endif
}

MediaResult<void> VaapiDevice::destroy_buffer(uint32_t buffer_id) {
#if defined(NISABA_HAS_VAAPI_HEADERS)
    if (!is_available() || !g_va_syms.vaDestroyBuffer) return MediaError::HardwareError;
    VADisplay dpy = reinterpret_cast<VADisplay>(va_display_);
    VAStatus st = g_va_syms.vaDestroyBuffer(dpy, static_cast<VABufferID>(buffer_id));
    if (st != VA_STATUS_SUCCESS) return MediaError::HardwareError;
    return MediaResult<void>::ok();
#else
    (void)buffer_id;
    return MediaError::NotImplemented;
#endif
}

MediaResult<uint32_t> VaapiDevice::create_coded_buffer(uint32_t context_id, uint32_t max_size) {
#if defined(NISABA_HAS_VAAPI_HEADERS)
    return create_buffer(context_id, VAEncCodedBufferType, max_size, 1, nullptr);
#else
    (void)context_id; (void)max_size;
    return MediaError::NotImplemented;
#endif
}

MediaResult<void*> VaapiDevice::map_buffer(uint32_t buffer_id) {
#if defined(NISABA_HAS_VAAPI_HEADERS)
    if (!is_available() || !g_va_syms.vaMapBuffer || buffer_id == 0xFFFFFFFF) return MediaError::HardwareError;
    VADisplay dpy = reinterpret_cast<VADisplay>(va_display_);
    void* pbuf = nullptr;
    VAStatus st = g_va_syms.vaMapBuffer(dpy, static_cast<VABufferID>(buffer_id), &pbuf);
    if (st != VA_STATUS_SUCCESS || !pbuf) return MediaError::HardwareError;
    return pbuf;
#else
    (void)buffer_id;
    return MediaError::NotImplemented;
#endif
}

MediaResult<void> VaapiDevice::unmap_buffer(uint32_t buffer_id) {
#if defined(NISABA_HAS_VAAPI_HEADERS)
    if (!is_available() || !g_va_syms.vaUnmapBuffer || buffer_id == 0xFFFFFFFF) return MediaError::HardwareError;
    VADisplay dpy = reinterpret_cast<VADisplay>(va_display_);
    VAStatus st = g_va_syms.vaUnmapBuffer(dpy, static_cast<VABufferID>(buffer_id));
    if (st != VA_STATUS_SUCCESS) return MediaError::HardwareError;
    return MediaResult<void>::ok();
#else
    (void)buffer_id;
    return MediaError::NotImplemented;
#endif
}

MediaResult<void> VaapiDevice::read_coded_buffer(uint32_t coded_buf_id, std::vector<uint8_t>& out_bytes) {
#if defined(NISABA_HAS_VAAPI_HEADERS)
    auto map_res = map_buffer(coded_buf_id);
    if (!map_res.is_ok()) return map_res.error();

    auto* seg = reinterpret_cast<VACodedBufferSegment*>(*map_res);
    while (seg) {
        if (seg->buf && seg->size > 0) {
            const auto* data = reinterpret_cast<const uint8_t*>(seg->buf);
            out_bytes.insert(out_bytes.end(), data, data + seg->size);
        }
        seg = reinterpret_cast<VACodedBufferSegment*>(seg->next);
    }

    (void)unmap_buffer(coded_buf_id);
    return MediaResult<void>::ok();
#else
    (void)coded_buf_id; (void)out_bytes;
    return MediaError::NotImplemented;
#endif
}

MediaResult<void> VaapiDevice::upload_frame_to_surface(uint32_t surface_id, const VideoFrame& frame) {
#if defined(NISABA_HAS_VAAPI_HEADERS)
    if (!is_available() || surface_id == 0xFFFFFFFF || !frame.is_valid()) return MediaError::InvalidData;
    VADisplay dpy = reinterpret_cast<VADisplay>(va_display_);

    if (g_va_syms.vaCreateImage && g_va_syms.vaMapBuffer && g_va_syms.vaUnmapBuffer &&
        g_va_syms.vaPutImage && g_va_syms.vaDestroyImage) {
        VAImageFormat fmt{};
        fmt.fourcc = VA_FOURCC_NV12;
        fmt.byte_order = VA_LSB_FIRST;
        fmt.bits_per_pixel = 12;

        VAImage img{};
        VAStatus st = g_va_syms.vaCreateImage(dpy, &fmt, frame.width(), frame.height(), &img);
        if (st == VA_STATUS_SUCCESS) {
            void* pbuf = nullptr;
            st = g_va_syms.vaMapBuffer(dpy, img.buf, &pbuf);
            if (st == VA_STATUS_SUCCESS && pbuf) {
                uint8_t* dst_base = reinterpret_cast<uint8_t*>(pbuf);
                uint32_t w = frame.width();
                uint32_t h = frame.height();

                if (frame.pixel_format() == PixelFormat::NV12) {
                    for (uint32_t p = 0; p < std::min(size_t{2}, frame.plane_count()); ++p) {
                        const uint8_t* src = frame.plane(p);
                        uint8_t* dst = dst_base + img.offsets[p];
                        size_t src_stride = frame.stride(p);
                        size_t dst_stride = img.pitches[p];
                        uint32_t plane_h = (p == 0) ? h : (h / 2);
                        size_t row_bytes = std::min(src_stride, dst_stride);
                        for (uint32_t y = 0; y < plane_h; ++y) {
                            std::memcpy(dst + y * dst_stride, src + y * src_stride, row_bytes);
                        }
                    }
                } else if (frame.pixel_format() == PixelFormat::YUV420P) {
                    // Copy Y
                    const uint8_t* src_y = frame.plane(0);
                    uint8_t* dst_y = dst_base + img.offsets[0];
                    size_t src_y_stride = frame.stride(0);
                    size_t dst_y_stride = img.pitches[0];
                    for (uint32_t y = 0; y < h; ++y) {
                        std::memcpy(dst_y + y * dst_y_stride, src_y + y * src_y_stride, std::min(static_cast<size_t>(w), dst_y_stride));
                    }
                    // Interleave U and V into UV
                    const uint8_t* src_u = frame.plane(1);
                    const uint8_t* src_v = frame.plane(2);
                    uint8_t* dst_uv = dst_base + img.offsets[1];
                    size_t src_u_stride = frame.stride(1);
                    size_t src_v_stride = frame.stride(2);
                    size_t dst_uv_stride = img.pitches[1];
                    for (uint32_t y = 0; y < h / 2; ++y) {
                        for (uint32_t x = 0; x < w / 2; ++x) {
                            dst_uv[y * dst_uv_stride + x * 2 + 0] = src_u[y * src_u_stride + x];
                            dst_uv[y * dst_uv_stride + x * 2 + 1] = src_v[y * src_v_stride + x];
                        }
                    }
                }

                g_va_syms.vaUnmapBuffer(dpy, img.buf);
                g_va_syms.vaPutImage(dpy, static_cast<VASurfaceID>(surface_id), img.image_id, 0, 0, w, h, 0, 0, w, h);
            }
            g_va_syms.vaDestroyImage(dpy, img.image_id);
            return MediaResult<void>::ok();
        }
    }
    return MediaError::HardwareError;
#else
    (void)surface_id; (void)frame;
    return MediaError::NotImplemented;
#endif
}

MediaResult<void> VaapiDevice::begin_picture(uint32_t context_id, uint32_t surface_id) {
#if defined(NISABA_HAS_VAAPI_HEADERS)
    if (!is_available() || !g_va_syms.vaBeginPicture) return MediaError::HardwareError;
    VADisplay dpy = reinterpret_cast<VADisplay>(va_display_);
    VAStatus st = g_va_syms.vaBeginPicture(dpy, static_cast<VAContextID>(context_id), static_cast<VASurfaceID>(surface_id));
    if (st != VA_STATUS_SUCCESS) return MediaError::HardwareError;
    return MediaResult<void>::ok();
#else
    (void)context_id; (void)surface_id;
    return MediaError::NotImplemented;
#endif
}

MediaResult<void> VaapiDevice::render_picture(uint32_t context_id, const uint32_t* buffers, uint32_t num_buffers) {
#if defined(NISABA_HAS_VAAPI_HEADERS)
    if (!is_available() || !g_va_syms.vaRenderPicture || !buffers || num_buffers == 0) return MediaError::HardwareError;
    VADisplay dpy = reinterpret_cast<VADisplay>(va_display_);

    std::vector<VABufferID> va_bufs;
    va_bufs.reserve(num_buffers);
    for (uint32_t i = 0; i < num_buffers; ++i) va_bufs.push_back(static_cast<VABufferID>(buffers[i]));

    VAStatus st = g_va_syms.vaRenderPicture(dpy, static_cast<VAContextID>(context_id), va_bufs.data(), static_cast<int>(num_buffers));
    if (st != VA_STATUS_SUCCESS) return MediaError::HardwareError;
    return MediaResult<void>::ok();
#else
    (void)context_id; (void)buffers; (void)num_buffers;
    return MediaError::NotImplemented;
#endif
}

MediaResult<void> VaapiDevice::end_picture(uint32_t context_id) {
#if defined(NISABA_HAS_VAAPI_HEADERS)
    if (!is_available() || !g_va_syms.vaEndPicture) return MediaError::HardwareError;
    VADisplay dpy = reinterpret_cast<VADisplay>(va_display_);
    VAStatus st = g_va_syms.vaEndPicture(dpy, static_cast<VAContextID>(context_id));
    if (st != VA_STATUS_SUCCESS) return MediaError::HardwareError;
    return MediaResult<void>::ok();
#else
    (void)context_id;
    return MediaError::NotImplemented;
#endif
}

MediaResult<void> VaapiDevice::sync_surface(uint32_t surface_id) {
#if defined(NISABA_HAS_VAAPI_HEADERS)
    if (!is_available() || !g_va_syms.vaSyncSurface) return MediaError::HardwareError;
    VADisplay dpy = reinterpret_cast<VADisplay>(va_display_);
    VAStatus st = g_va_syms.vaSyncSurface(dpy, static_cast<VASurfaceID>(surface_id));
    if (st != VA_STATUS_SUCCESS) return MediaError::HardwareError;
    return MediaResult<void>::ok();
#else
    (void)surface_id;
    return MediaError::NotImplemented;
#endif
}

MediaResult<void> VaapiDevice::export_surface_dma_buf(uint32_t surface_id, HwBufferDescriptor& desc) {
#if defined(NISABA_HAS_VAAPI_HEADERS)
    if (!is_available() || surface_id == 0) return MediaError::InvalidData;
    VADisplay dpy = reinterpret_cast<VADisplay>(va_display_);

    if (g_va_syms.vaExportSurfaceHandle) {
#if defined(VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2)
        VADRMPRIMESurfaceDescriptor prime_desc{};
        VAStatus st = g_va_syms.vaExportSurfaceHandle(
            dpy,
            static_cast<VASurfaceID>(surface_id),
            VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
            VA_EXPORT_SURFACE_READ_ONLY | VA_EXPORT_SURFACE_SEPARATE_LAYERS,
            &prime_desc
        );
        if (st == VA_STATUS_SUCCESS) {
            desc.width = prime_desc.width;
            desc.height = prime_desc.height;
            desc.drm_fourcc = prime_desc.fourcc;
            if (prime_desc.num_objects > 0) {
                for (uint32_t i = 0; i < std::min(prime_desc.num_objects, 4u); ++i) {
                    desc.fds[i] = prime_desc.objects[i].fd;
                    desc.modifier = prime_desc.objects[i].drm_format_modifier;
                }
            }
            if (prime_desc.num_layers > 0) {
                for (uint32_t l = 0; l < std::min(prime_desc.num_layers, 4u); ++l) {
                    desc.strides[l] = prime_desc.layers[l].pitch[0];
                    desc.offsets[l] = prime_desc.layers[l].offset[0];
                }
            }
            return MediaResult<void>::ok();
        }
#endif
    }
    if (drm_fd_ >= 0) {
        desc.fds[0] = drm_fd_;
        return MediaResult<void>::ok();
    }
#else
    (void)surface_id; (void)desc;
#endif
    return MediaError::HardwareError;
}

MediaResult<VideoFrame> VaapiDevice::derive_surface_to_frame(uint32_t surface_id, uint32_t width, uint32_t height) {
    if (!is_available() || surface_id == 0) return MediaError::InvalidData;

#if defined(NISABA_HAS_VAAPI_HEADERS)
    VADisplay dpy = reinterpret_cast<VADisplay>(va_display_);

    if (g_va_syms.vaDeriveImage && g_va_syms.vaMapBuffer && g_va_syms.vaUnmapBuffer && g_va_syms.vaDestroyImage) {
        VAImage image{};
        VAStatus st = g_va_syms.vaDeriveImage(dpy, static_cast<VASurfaceID>(surface_id), &image);
        if (st == VA_STATUS_SUCCESS) {
            void* pbuf = nullptr;
            st = g_va_syms.vaMapBuffer(dpy, image.buf, &pbuf);
            if (st == VA_STATUS_SUCCESS && pbuf) {
                PixelFormat fmt = PixelFormat::NV12;
                if (image.format.fourcc == VA_FOURCC_YV12) fmt = PixelFormat::YUV420P;
                else if (image.format.fourcc == VA_FOURCC_P010) fmt = PixelFormat::P010;

                VideoFrame frame = VideoFrame::allocate(width, height, fmt);
                uint8_t* src_base = reinterpret_cast<uint8_t*>(pbuf);

                for (uint32_t i = 0; i < image.num_planes && i < frame.plane_count(); ++i) {
                    const uint8_t* src_plane = src_base + image.offsets[i];
                    uint8_t* dst_plane = frame.plane_mut(i);
                    size_t src_stride = image.pitches[i];
                    size_t dst_stride = frame.stride(i);
                    uint32_t plane_h = frame.plane_height(i);
                    uint32_t copy_bytes = std::min(src_stride, dst_stride);

                    for (uint32_t y = 0; y < plane_h; ++y) {
                        std::memcpy(dst_plane + y * dst_stride, src_plane + y * src_stride, copy_bytes);
                    }
                }

                g_va_syms.vaUnmapBuffer(dpy, image.buf);
                g_va_syms.vaDestroyImage(dpy, image.image_id);

                frame.set_hw_frame(true);
                frame.set_hw_surface_id(surface_id);

                HwBufferDescriptor desc{};
                if (export_surface_dma_buf(surface_id, desc).is_ok()) {
                    frame.set_dma_buf_fd(desc.fds[0]);
                }
                return frame;
            }
            g_va_syms.vaDestroyImage(dpy, image.image_id);
        }
    }

    // Direct hardware-backed frame descriptor
    VideoFrame frame = VideoFrame::allocate(width, height, PixelFormat::NV12);
    frame.set_hw_frame(true);
    frame.set_hw_surface_id(surface_id);

    HwBufferDescriptor desc{};
    if (export_surface_dma_buf(surface_id, desc).is_ok()) {
        frame.set_dma_buf_fd(desc.fds[0]);
    }
    return frame;
#else
    (void)width; (void)height;
    return MediaError::NotImplemented;
#endif
}

MediaResult<void> VaapiDevice::export_buffer(const VideoFrame& frame, HwBufferDescriptor& desc) {
    if (!frame.is_valid()) return MediaError::InvalidData;

    desc.width = frame.width();
    desc.height = frame.height();
    for (size_t i = 0; i < frame.plane_count(); ++i) {
        desc.strides[i] = static_cast<uint32_t>(frame.stride(i));
        desc.offsets[i] = 0;
    }

    if (frame.hw_surface_id() > 0) {
        if (export_surface_dma_buf(frame.hw_surface_id(), desc).is_ok()) {
            return MediaResult<void>::ok();
        }
    }

    if (frame.dma_buf_fd() >= 0) {
        desc.fds[0] = frame.dma_buf_fd();
        return MediaResult<void>::ok();
    }

    if (drm_fd_ >= 0) {
        desc.fds[0] = drm_fd_;
    }
    return MediaResult<void>::ok();
}

MediaResult<VideoFrame> VaapiDevice::import_buffer(const HwBufferDescriptor& desc) {
    if (desc.width == 0 || desc.height == 0) return MediaError::InvalidData;
    VideoFrame frame = VideoFrame::allocate(desc.width, desc.height, PixelFormat::NV12);
    frame.set_hw_frame(true);
    if (desc.fds[0] >= 0) {
        frame.set_dma_buf_fd(desc.fds[0]);
    }
    return frame;
}

std::unique_ptr<VaapiDevice> VaapiDevice::create() {
    auto dev = std::make_unique<VaapiDevice>();
    return dev;
}

} // namespace atum
