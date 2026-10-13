#pragma once

#include <vector>
#include <queue>
#include <cstdint>
#include <mutex>
#include <memory>
#include "atum/core/media_types.hpp"
#include "atum/hwaccel/vaapi_device.hpp"

namespace atum {

inline constexpr uint32_t HW_INVALID_SURFACE_ID = 0xFFFFFFFF;

/// Sovereign Hardware Surface Pool for zero-allocation GPU memory surface recycling.
class HwSurfacePool {
public:
    HwSurfacePool() = default;
    ~HwSurfacePool() {
        release();
    }

    HwSurfacePool(const HwSurfacePool&) = delete;
    HwSurfacePool& operator=(const HwSurfacePool&) = delete;

    HwSurfacePool(HwSurfacePool&& other) noexcept {
        std::lock_guard<std::mutex> lock(other.mutex_);
        device_ = other.device_;
        surfaces_ = std::move(other.surfaces_);
        available_ = std::move(other.available_);
        other.device_ = nullptr;
    }

    HwSurfacePool& operator=(HwSurfacePool&& other) noexcept {
        if (this != &other) {
            release();
            std::scoped_lock lock(mutex_, other.mutex_);
            device_ = other.device_;
            surfaces_ = std::move(other.surfaces_);
            available_ = std::move(other.available_);
            other.device_ = nullptr;
        }
        return *this;
    }

    MediaResult<void> init(VaapiDevice* device, uint32_t width, uint32_t height, PixelFormat format, size_t pool_size) {
        if (!device || pool_size == 0) return MediaError::InvalidData;
        release();

        device_ = device;
        auto surf_res = device_->create_surfaces(width, height, format, static_cast<uint32_t>(pool_size));
        if (!surf_res.is_ok()) return surf_res.error();

        std::lock_guard<std::mutex> lock(mutex_);
        surfaces_ = std::move(*surf_res);
        while (!available_.empty()) available_.pop();
        for (uint32_t s : surfaces_) {
            available_.push(s);
        }
        return MediaResult<void>::ok();
    }

    void release() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (device_ && !surfaces_.empty()) {
            (void)device_->destroy_surfaces(surfaces_);
            surfaces_.clear();
            while (!available_.empty()) available_.pop();
        }
        device_ = nullptr;
    }

    uint32_t acquire_surface() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (available_.empty()) return HW_INVALID_SURFACE_ID;
        uint32_t s = available_.front();
        available_.pop();
        return s;
    }

    void return_surface(uint32_t surface_id) {
        if (surface_id == HW_INVALID_SURFACE_ID) return;
        std::lock_guard<std::mutex> lock(mutex_);
        available_.push(surface_id);
    }

    [[nodiscard]] const std::vector<uint32_t>& all_surfaces() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return surfaces_;
    }

    [[nodiscard]] size_t available_count() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return available_.size();
    }

    [[nodiscard]] size_t total_count() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return surfaces_.size();
    }

    [[nodiscard]] bool is_valid() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return !surfaces_.empty();
    }

private:
    VaapiDevice* device_{nullptr};
    std::vector<uint32_t> surfaces_;
    std::queue<uint32_t> available_;
    mutable std::mutex mutex_;
};

} // namespace atum
