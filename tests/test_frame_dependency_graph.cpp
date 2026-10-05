#include "atum/parallel/frame_graph.hpp"
#include <iostream>
#include <cassert>
#include <chrono>

using namespace atum;

void test_frame_dependency_dag() {
    std::cout << "Testing Asynchronous Frame Dependency Graph (IBBP GOP Structure)..." << std::endl;

    ThreadPool pool(4);
    FrameGraphScheduler scheduler(pool);

    // Sequence in DTS order:
    // Frame 0: I-0 (PTS 0, DTS 0) - Keyframe (0 refs)
    // Frame 1: P-3 (PTS 3000, DTS 1000) - Reference P-frame (refs: Frame 0)
    // Frame 2: B-1 (PTS 1000, DTS 2000) - Non-ref B-frame (refs: Frame 0, Frame 1)
    // Frame 3: B-2 (PTS 2000, DTS 3000) - Non-ref B-frame (refs: Frame 0, Frame 1)

    auto make_job = [](int64_t pts, int delay_ms) {
        return [pts, delay_ms]() {
            if (delay_ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            }
            auto f = VideoFrame::allocate(64, 64, PixelFormat::YUV420P);
            f.set_pts(pts);
            return f;
        };
    };

    scheduler.submit_frame(0, 0, 0, true, true, {}, make_job(0, 20));
    scheduler.submit_frame(1, 3000, 1000, false, true, {0}, make_job(3000, 20));
    scheduler.submit_frame(2, 1000, 2000, false, false, {0, 1}, make_job(1000, 30));
    scheduler.submit_frame(3, 2000, 3000, false, false, {0, 1}, make_job(2000, 30));

    scheduler.wait_all();

    // Verify frames are received in strictly increasing presentation order (PTS: 0, 1000, 2000, 3000)
    std::vector<int64_t> received_pts;
    while (true) {
        VideoFrame vf;
        MediaResult<void> res = scheduler.receive_presentation_frame(vf);
        if (!res.is_ok()) break;
        received_pts.push_back(vf.pts());
    }

    assert(received_pts.size() == 4);
    assert(received_pts[0] == 0);
    assert(received_pts[1] == 1000);
    assert(received_pts[2] == 2000);
    assert(received_pts[3] == 3000);

    assert(scheduler.max_concurrency_achieved() >= 2);

    std::cout << "  -> PTS Presentation order validated: 0 -> 1000 -> 2000 -> 3000." << std::endl;
    std::cout << "  -> Maximum concurrency achieved: " << scheduler.max_concurrency_achieved() << " simultaneous frames." << std::endl;
}

int main() {
    std::cout << "=== Running Frame Dependency Graph Tests ===" << std::endl;
    test_frame_dependency_dag();
    std::cout << "[SUCCESS] All Frame Dependency Graph tests passed!" << std::endl;
    return 0;
}
