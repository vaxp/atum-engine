#include <cassert>
#include <iostream>
#include <thread>
#include <cmath>
#include "atum/audio/av_clock.hpp"

using namespace atum;

static void test_clock_basics_and_state() {
    std::cout << "[A/V Clock Test] 1. Clock Basics & State..." << std::endl;

    AvClock clock(ClockMaster::Audio);
    assert(clock.master() == ClockMaster::Audio);
    assert(clock.is_paused());
    assert(clock.playback_rate() == 1.0);

    clock.play();
    assert(!clock.is_paused());

    clock.pause();
    assert(clock.is_paused());

    clock.set_playback_rate(1.5);
    assert(clock.playback_rate() == 1.5);

    clock.seek_seconds(10.0);
    assert(clock.get_current_time_seconds() == 10.0);

    std::cout << "  -> Clock state and seek verified." << std::endl;
}

static void test_audio_pts_tracking() {
    std::cout << "[A/V Clock Test] 2. Audio PTS Tracking & PLL..." << std::endl;

    AvClock clock(ClockMaster::Audio);
    clock.play();

    // Set initial audio PTS: 5.0 seconds, rendered 0 samples at 48 kHz
    clock.update_audio_time(5.0, 0, 48000);
    double t0 = clock.get_current_time_seconds();
    assert(std::abs(t0 - 5.0) < 0.05);

    // Update with 4800 rendered samples (0.1 seconds of audio)
    clock.update_audio_time(5.0, 4800, 48000);
    double t1 = clock.get_current_time_seconds();
    assert(std::abs(t1 - 5.1) < 0.05);

    std::cout << "  -> Audio PTS and sample consumption tracking verified." << std::endl;
}

static void test_video_pacing_decisions() {
    std::cout << "[A/V Clock Test] 3. Video Pacing Decisions (Display / Hold / Drop)..." << std::endl;

    AvClock clock(ClockMaster::Audio);
    clock.play();
    clock.seek_seconds(2.0); // Clock at 2.000s
    clock.set_sync_threshold_ms(15.0); // +/- 15ms is on-time
    clock.set_drop_threshold_ms(100.0); // Late by > 100ms is dropped

    // 1. Frame on-time: 2.005s (+5ms difference) -> DisplayNow
    auto res_on_time = clock.evaluate_video_frame(2.005);
    assert(res_on_time.action == VideoPacingAction::DisplayNow);
    assert(res_on_time.delay_seconds == 0.0);

    // 2. Frame ahead: 2.060s (+60ms difference) -> Hold
    auto res_ahead = clock.evaluate_video_frame(2.060);
    assert(res_ahead.action == VideoPacingAction::Hold);
    assert(res_ahead.delay_seconds > 0.05 && res_ahead.delay_seconds < 0.07);

    // 3. Frame too late: 1.850s (-150ms difference) -> Drop
    auto res_late = clock.evaluate_video_frame(1.850);
    assert(res_late.action == VideoPacingAction::Drop);
    assert(res_late.delay_seconds == 0.0);

    std::cout << "  -> Video pacing evaluation verified." << std::endl;
}

int main() {
    std::cout << "======================================" << std::endl;
    std::cout << "   Running Master A/V Clock Tests     " << std::endl;
    std::cout << "======================================" << std::endl;

    test_clock_basics_and_state();
    test_audio_pts_tracking();
    test_video_pacing_decisions();

    std::cout << "\n>>> ALL MASTER A/V CLOCK TESTS PASSED! <<<\n" << std::endl;
    return 0;
}
