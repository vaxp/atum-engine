#include "atum/hwaccel/vaapi_device.hpp"
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>
#endif
#include <cstring>
#include <iostream>

#if __has_include(<va/va.h>) && __has_include(<va/va_drm.h>)
#include <va/va.h>
#include <va/va_drm.h>
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
typedef VAStatus (*FnVaMaxNumProfiles)(VADisplay dpy, int* num_profiles);
typedef VAStatus (*FnVaQueryConfigProfiles)(VADisplay dpy, VAProfile* profile_list, int* num_profiles);

struct VaSymbols {
    FnVaGetDisplayDRM vaGetDisplayDRM{nullptr};
    FnVaInitialize vaInitialize{nullptr};
    FnVaTerminate vaTerminate{nullptr};
    FnVaErrorStr vaErrorStr{nullptr};
    FnVaMaxNumProfiles vaMaxNumProfiles{nullptr};
    FnVaQueryConfigProfiles vaQueryConfigProfiles{nullptr};

    bool load(void* libva, void* libva_drm) noexcept {
        if (!libva || !libva_drm) return false;

        vaGetDisplayDRM = reinterpret_cast<FnVaGetDisplayDRM>(dlsym(libva_drm, "vaGetDisplayDRM"));
        vaInitialize = reinterpret_cast<FnVaInitialize>(dlsym(libva, "vaInitialize"));
        vaTerminate = reinterpret_cast<FnVaTerminate>(dlsym(libva, "vaTerminate"));
        vaErrorStr = reinterpret_cast<FnVaErrorStr>(dlsym(libva, "vaErrorStr"));
        vaMaxNumProfiles = reinterpret_cast<FnVaMaxNumProfiles>(dlsym(libva, "vaMaxNumProfiles"));
        vaQueryConfigProfiles = reinterpret_cast<FnVaQueryConfigProfiles>(dlsym(libva, "vaQueryConfigProfiles"));

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
    int max_profiles = 0;
    if (g_va_syms.vaMaxNumProfiles(dpy, &max_profiles) != VA_STATUS_SUCCESS || max_profiles <= 0) {
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

MediaResult<void> VaapiDevice::export_buffer(const VideoFrame& frame, HwBufferDescriptor& desc) {
    if (!frame.is_valid()) return MediaError::InvalidData;

    desc.width = frame.width();
    desc.height = frame.height();
    for (size_t i = 0; i < frame.plane_count(); ++i) {
        desc.strides[i] = static_cast<uint32_t>(frame.stride(i));
        desc.offsets[i] = 0;
    }

    if (drm_fd_ >= 0) {
        desc.fds[0] = drm_fd_;
    }
    return MediaResult<void>::ok();
}

MediaResult<VideoFrame> VaapiDevice::import_buffer(const HwBufferDescriptor& desc) {
    if (desc.width == 0 || desc.height == 0) return MediaError::InvalidData;
    VideoFrame frame = VideoFrame::allocate(desc.width, desc.height, PixelFormat::NV12);
    return frame;
}

std::unique_ptr<VaapiDevice> VaapiDevice::create() {
    auto dev = std::make_unique<VaapiDevice>();
    return dev;
}

} // namespace atum
