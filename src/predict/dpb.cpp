#include "atum/predict/dpb.hpp"
#include <algorithm>

namespace atum {

void DecodedPictureBuffer::insert(VideoFrame frame, int32_t poc, uint32_t frame_num, bool is_keyframe) {
    if (is_keyframe) {
        entries_.clear();
    }

    if (entries_.size() >= max_ref_frames_) {
        // Evict oldest short-term picture
        entries_.erase(entries_.begin());
    }

    DpbEntry entry{};
    entry.frame = std::move(frame);
    entry.poc = poc;
    entry.frame_num = frame_num;
    entry.is_keyframe = is_keyframe;
    entry.is_long_term = false;

    entries_.push_back(std::move(entry));
}

const VideoFrame* DecodedPictureBuffer::find_by_poc(int32_t poc) const noexcept {
    for (const auto& entry : entries_) {
        if (entry.poc == poc) {
            return &entry.frame;
        }
    }
    return nullptr;
}

const VideoFrame* DecodedPictureBuffer::get_ref(size_t index) const noexcept {
    if (index < entries_.size()) {
        // Index 0 returns the most recent frame
        return &entries_[entries_.size() - 1 - index].frame;
    }
    return nullptr;
}

void DecodedPictureBuffer::clear() noexcept {
    entries_.clear();
}

} // namespace atum
