#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <memory>
#include <optional>
#include "atum/surface/video_frame.hpp"

namespace atum {

/// Reference picture entry inside the Decoded Picture Buffer.
struct DpbEntry {
    VideoFrame frame;
    int32_t poc{0};
    uint32_t frame_num{0};
    bool is_long_term{false};
    bool is_keyframe{false};
};

/// Sovereign Decoded Picture Buffer (DPB) for reference picture management.
class DecodedPictureBuffer {
public:
    explicit DecodedPictureBuffer(size_t max_ref_frames = 16) noexcept
        : max_ref_frames_(max_ref_frames) {}

    /// Inserts a newly decoded frame into the reference buffer.
    void insert(VideoFrame frame, int32_t poc, uint32_t frame_num, bool is_keyframe = false);

    /// Finds a reference frame by POC.
    [[nodiscard]] const VideoFrame* find_by_poc(int32_t poc) const noexcept;

    /// Finds a reference frame by index (0 = most recent).
    [[nodiscard]] const VideoFrame* get_ref(size_t index) const noexcept;

    /// Clears all reference frames (e.g. upon IDR / keyframe).
    void clear() noexcept;

    [[nodiscard]] size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] const std::vector<DpbEntry>& entries() const noexcept { return entries_; }

private:
    size_t max_ref_frames_{16};
    std::vector<DpbEntry> entries_;
};

} // namespace atum
