#include "atum/audio/av_clock.hpp"
#include <algorithm>
#include <cmath>

namespace atum {

AvClock::AvClock(ClockMaster master)
    : master_(master),
      paused_(true),
      playback_rate_(1.0),
      last_wall_time_(std::chrono::steady_clock::now()),
      audio_update_wall_time_(last_wall_time_),
      video_update_wall_time_(last_wall_time_) {}

void AvClock::set_master(ClockMaster master) noexcept {
    master_ = master;
}

void AvClock::play() noexcept {
    if (paused_) {
        last_wall_time_ = std::chrono::steady_clock::now();
        audio_update_wall_time_ = last_wall_time_;
        video_update_wall_time_ = last_wall_time_;
        paused_ = false;
    }
}

void AvClock::pause() noexcept {
    if (!paused_) {
        // Freeze current master presentation time
        current_master_time_ = get_current_time_seconds();
        paused_ = true;
    }
}

void AvClock::set_playback_rate(double rate) noexcept {
    if (rate > 0.05 && rate < 20.0) {
        current_master_time_ = get_current_time_seconds();
        playback_rate_ = rate;
    }
}

void AvClock::seek_seconds(double seconds) noexcept {
    current_master_time_ = std::max(0.0, seconds);
    audio_pts_ = current_master_time_;
    video_pts_ = current_master_time_;
    last_wall_time_ = std::chrono::steady_clock::now();
    audio_update_wall_time_ = last_wall_time_;
    video_update_wall_time_ = last_wall_time_;
    pll_drift_accum_ = 0.0;
    pll_slew_factor_ = 1.0;
}

void AvClock::update_audio_time(double pts_seconds, size_t samples_rendered, uint32_t sample_rate) noexcept {
    double sample_offset = (sample_rate > 0) ? (static_cast<double>(samples_rendered) / sample_rate) : 0.0;
    audio_pts_ = pts_seconds + sample_offset;
    audio_update_wall_time_ = std::chrono::steady_clock::now();

    if (master_ == ClockMaster::Audio) {
        // PLL drift calculation: smooth slewing within [-5%, +5%]
        double drift = audio_pts_ - current_master_time_;
        pll_drift_accum_ = 0.9 * pll_drift_accum_ + 0.1 * drift;
        pll_slew_factor_ = 1.0 + std::clamp(pll_drift_accum_ * 0.1, -0.05, 0.05);
    }
}

void AvClock::update_video_time(double pts_seconds) noexcept {
    video_pts_ = pts_seconds;
    video_update_wall_time_ = std::chrono::steady_clock::now();
}

double AvClock::get_current_time_seconds() const noexcept {
    if (paused_) {
        return current_master_time_;
    }

    auto now = std::chrono::steady_clock::now();
    double elapsed_wall = std::chrono::duration<double>(now - last_wall_time_).count();
    last_wall_time_ = now;

    if (master_ == ClockMaster::Audio) {
        double elapsed_since_audio = std::chrono::duration<double>(now - audio_update_wall_time_).count();
        double audio_clock = audio_pts_ + elapsed_since_audio * playback_rate_ * pll_slew_factor_;
        const_cast<AvClock*>(this)->current_master_time_ = audio_clock;
    } else {
        const_cast<AvClock*>(this)->current_master_time_ += elapsed_wall * playback_rate_;
    }

    return current_master_time_;
}

VideoPacingResult AvClock::evaluate_video_frame(
    double frame_pts_seconds,
    double frame_duration_seconds
) const noexcept {
    double master_time = get_current_time_seconds();
    double diff_seconds = frame_pts_seconds - master_time;
    double diff_ms = diff_seconds * 1000.0;

    VideoPacingResult result{};
    result.time_difference_ms = diff_ms;

    // 1. Frame is late by more than drop threshold -> Drop
    if (diff_ms < -drop_threshold_ms_) {
        result.action = VideoPacingAction::Drop;
        result.delay_seconds = 0.0;
        return result;
    }

    // 2. Frame is ahead by more than sync threshold -> Hold
    if (diff_ms > sync_threshold_ms_) {
        result.action = VideoPacingAction::Hold;
        result.delay_seconds = std::max(0.0, diff_seconds / playback_rate_);
        return result;
    }

    // 3. Frame is within on-time window -> Display Now
    result.action = VideoPacingAction::DisplayNow;
    result.delay_seconds = 0.0;
    (void)frame_duration_seconds;
    return result;
}

} // namespace atum
