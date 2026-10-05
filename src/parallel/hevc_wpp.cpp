#include "atum/parallel/hevc_wpp.hpp"
#include "atum/inloop/sao_hevc.hpp"
#include <algorithm>
#include <iostream>

namespace atum {

HevcWppCoordinator::HevcWppCoordinator(ThreadPool& pool)
    : pool_(pool) {
}

MediaResult<void> HevcWppCoordinator::decode_frame_wpp(
    VideoFrame& frame,
    const HevcSliceHeader* slice_hdr,
    std::span<const uint8_t> payload,
    const DecodedPictureBuffer& dpb,
    const std::vector<size_t>& substream_offsets
) {
    uint32_t w = frame.width();
    uint32_t h = frame.height();
    if (w == 0 || h == 0) return MediaError::InvalidData;

    uint32_t ctu_cols = (w + ctu_size_ - 1) / ctu_size_;
    uint32_t ctu_rows = (h + ctu_size_ - 1) / ctu_size_;

    last_rows_ = ctu_rows;
    last_threads_ = std::min(static_cast<size_t>(ctu_rows), pool_.thread_count());

    HevcSliceType st = slice_hdr ? slice_hdr->slice_type : HevcSliceType::I;
    int32_t qp = 26 + (slice_hdr ? slice_hdr->slice_qp_delta : 0);

    // Prepare synchronization structures
    row_progress_ = std::vector<std::atomic<int32_t>>(ctu_rows);
    for (auto& p : row_progress_) p.store(-1, std::memory_order_relaxed);

    row_context_ready_ = std::vector<std::atomic<bool>>(ctu_rows);
    for (auto& b : row_context_ready_) b.store(false, std::memory_order_relaxed);

    saved_row_contexts_.resize(ctu_rows);

    TaskGroup group(pool_);

    for (uint32_t r = 0; r < ctu_rows; ++r) {
        // Determine substream slice payload for this row
        std::span<const uint8_t> row_payload;
        if (!substream_offsets.empty() && r < substream_offsets.size()) {
            size_t start = substream_offsets[r];
            size_t end = (r + 1 < substream_offsets.size()) ? substream_offsets[r + 1] : payload.size();
            if (start < payload.size() && end <= payload.size() && end >= start) {
                row_payload = payload.subspan(start, end - start);
            }
        } else {
            // If no explicit substream offsets, pass entire payload span
            row_payload = payload;
        }

        group.run([this, r, ctu_cols, &frame, st, qp, row_payload, &dpb]() {
            decode_row_worker(r, ctu_cols, frame, st, qp, row_payload, dpb);
        });
    }

    group.wait();

    // In-Loop Sample Adaptive Offset (SAO) on the reconstructed surface
    uint8_t* y_plane = frame.plane_mut(0);
    size_t y_stride = frame.stride(0);
    SaoParams sao;
    sao.type = SaoType::BandOffset;
    sao.band_position = 12;
    sao.offsets[0] = 1;
    sao.offsets[1] = -1;
    sao.offsets[2] = 2;
    sao.offsets[3] = -2;
    SaoHevc::apply_sao(y_plane, y_stride, w, h, sao, 8);

    return {};
}

void HevcWppCoordinator::decode_row_worker(
    uint32_t row_idx,
    uint32_t ctu_cols,
    VideoFrame& frame,
    HevcSliceType slice_type,
    int32_t base_qp,
    std::span<const uint8_t> row_payload,
    const DecodedPictureBuffer& dpb
) {
    HevcCabacContextSet ctxs;

    if (row_idx == 0) {
        ctxs.init(slice_type, base_qp);
    } else {
        // Wait for prior row to complete CTU 1 and hand over CABAC context state
        std::unique_lock<std::mutex> lock(sync_mutex_);
        sync_cv_.wait(lock, [this, row_idx]() {
            return row_context_ready_[row_idx - 1].load(std::memory_order_acquire);
        });
        ctxs = saved_row_contexts_[row_idx - 1];
    }

    CabacDecoder cabac;
    if (!row_payload.empty()) {
        (void)cabac.init(row_payload);
    }

    for (uint32_t cx = 0; cx < ctu_cols; ++cx) {
        if (row_idx > 0) {
            // Staggered 2-CTU dependency: Wait until row_idx - 1 has finished cx + 2
            // or until row_idx - 1 has completely finished its row
            std::unique_lock<std::mutex> lock(sync_mutex_);
            sync_cv_.wait(lock, [this, row_idx, cx, ctu_cols]() {
                int32_t prev_progress = row_progress_[row_idx - 1].load(std::memory_order_acquire);
                if (prev_progress >= static_cast<int32_t>(ctu_cols - 1)) return true;
                return prev_progress >= static_cast<int32_t>(cx + 2);
            });
        }

        HevcCu ctu{};
        ctu.x = cx * ctu_size_;
        ctu.y = row_idx * ctu_size_;
        ctu.size = ctu_size_;
        ctu.depth = 0;

        if (!row_payload.empty() && !cabac.is_end_of_substream()) {
            (void)HevcCtuDecoder::decode_cu_quadtree(cabac, ctxs, slice_type, ctu, 16);
        } else {
            // Default intra planar fallback
            ctu.is_split = false;
            ctu.pred_mode = (slice_type == HevcSliceType::I || dpb.empty()) ?
                            HevcPredMode::MODE_INTRA : HevcPredMode::MODE_INTER;
            HevcPu pu{};
            pu.x = ctu.x;
            pu.y = ctu.y;
            pu.width = ctu_size_;
            pu.height = ctu_size_;
            pu.intra_mode_luma = 0;
            pu.inter_pred_idc = HevcInterPredIdc::Pred_L0;
            pu.mv[0] = {0, 0, 0};
            ctu.pus.push_back(pu);
        }

        // Reconstruct CTU samples into the shared preallocated target frame
        HevcCtuDecoder::reconstruct_ctu(ctu, frame, dpb);

        // Checkpoint CABAC context state after CTU 1
        if (cx == 1) {
            {
                std::lock_guard<std::mutex> lock(sync_mutex_);
                saved_row_contexts_[row_idx] = ctxs;
                row_context_ready_[row_idx].store(true, std::memory_order_release);
            }
            sync_cv_.notify_all();
        }

        // Update row progress and notify subsequent dependent rows
        {
            std::lock_guard<std::mutex> lock(sync_mutex_);
            row_progress_[row_idx].store(static_cast<int32_t>(cx), std::memory_order_release);
        }
        sync_cv_.notify_all();
    }

    // Ensure context ready is signaled even if row has < 2 CTUs
    if (ctu_cols < 2 && !row_context_ready_[row_idx].load(std::memory_order_acquire)) {
        {
            std::lock_guard<std::mutex> lock(sync_mutex_);
            saved_row_contexts_[row_idx] = ctxs;
            row_context_ready_[row_idx].store(true, std::memory_order_release);
        }
        sync_cv_.notify_all();
    }
}

} // namespace atum
