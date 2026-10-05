#pragma once

#include <cstdint>
#include <cstddef>
#include <chrono>
#include <atomic>
#include "atum/core/media_types.hpp"

namespace atum {

/// Synchronization Master Source.
enum class ClockMaster : uint8_t {
    Audio = 0,    ///< Audio master (industry standard, soundcard clock drives playback)
    Video = 1,    ///< Video master
    External = 2  ///< Monotonic system wall-clock
};

/// Video frame pacing action determined by master clock.
enum class VideoPacingAction : uint8_t {
    DisplayNow = 0, ///< Frame is on schedule, present immediately
    Hold = 1,       ///< Frame is ahead of clock, wait before display
    Drop = 2        ///< Frame is too late, drop to catch up with audio
};

/// Pacing decision with recommended delay.
struct VideoPacingResult {
    VideoPacingAction action{VideoPacingAction::DisplayNow};
    double delay_seconds{0.0};
    double time_difference_ms{0.0}; ///< video_time - master_clock_time (ms)
};

/// Sovereign Master Audio/Video Presentation Clock Synchronization Engine.
/// Implements continuous drift estimation, Phase-Locked Loop (PLL) slewing,
/// and frame-level pacing decision logic.
class AvClock {
public:
    explicit AvClock(ClockMaster master = ClockMaster::Audio);

    /// Sets the master synchronization reference source.
    void set_master(ClockMaster master) noexcept;
    [[nodiscard]] ClockMaster master() const noexcept { return master_; }

    /// Starts or resumes playback.
    void play() noexcept;

    /// Pauses playback clock.
    void pause() noexcept;

    /// Checks whether the clock is currently paused.
    [[nodiscard]] bool is_paused() const noexcept { return paused_; }

    /// Sets playback speed multiplier (e.g. 1.0 = normal, 1.5 = 1.5x, 0.5 = half speed).
    void set_playback_rate(double rate) noexcept;
    [[nodiscard]] double playback_rate() const noexcept { return playback_rate_; }

    /// Seeks clock to a given PTS in fractional seconds.
    void seek_seconds(double seconds) noexcept;

    /// Updates audio presentation time from decoded/rendered audio samples.
    void update_audio_time(double pts_seconds, size_t samples_rendered, uint32_t sample_rate) noexcept;

    /// Updates video presentation time from displayed video frame.
    void update_video_time(double pts_seconds) noexcept;

    /// Returns the current master presentation time in seconds.
    [[nodiscard]] double get_current_time_seconds() const noexcept;

    /// Evaluates a candidate video frame against the master presentation clock.
    /// Returns whether the video renderer should display, hold, or drop the frame.
    [[nodiscard]] VideoPacingResult evaluate_video_frame(
        double frame_pts_seconds,
        double frame_duration_seconds = 0.041666 // ~24 fps default
    ) const noexcept;

    /// Configures drop threshold in milliseconds (default: 100 ms).
    void set_drop_threshold_ms(double ms) noexcept { drop_threshold_ms_ = ms; }

    /// Configures on-time threshold in milliseconds (default: 15 ms).
    void set_sync_threshold_ms(double ms) noexcept { sync_threshold_ms_ = ms; }

private:
    ClockMaster master_{ClockMaster::Audio};
    bool paused_{true};
    double playback_rate_{1.0};

    double drop_threshold_ms_{100.0};
    double sync_threshold_ms_{15.0};

    mutable std::chrono::steady_clock::time_point last_wall_time_{};
    double current_master_time_{0.0};

    double audio_pts_{0.0};
    std::chrono::steady_clock::time_point audio_update_wall_time_{};

    double video_pts_{0.0};
    std::chrono::steady_clock::time_point video_update_wall_time_{};

    // PLL Drift Slewing state
    double pll_drift_accum_{0.0};
    double pll_slew_factor_{1.0};
};

} // namespace atum
