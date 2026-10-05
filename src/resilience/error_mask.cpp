#include "atum/resilience/error_mask.hpp"

namespace atum {

ErrorConcealmentMask::ErrorConcealmentMask(uint32_t width, uint32_t height, uint32_t block_size) {
    reset(width, height, block_size);
}

void ErrorConcealmentMask::reset(uint32_t width, uint32_t height, uint32_t block_size) {
    width_ = width;
    height_ = height;
    block_size_ = block_size > 0 ? block_size : 16;
    cols_ = (width_ + block_size_ - 1) / block_size_;
    rows_ = (height_ + block_size_ - 1) / block_size_;
    grid_.assign(static_cast<size_t>(cols_) * rows_, BlockStatus::Valid);
}

BlockStatus ErrorConcealmentMask::get(uint32_t col, uint32_t row) const noexcept {
    if (col >= cols_ || row >= rows_) {
        return BlockStatus::Valid;
    }
    return grid_[static_cast<size_t>(row) * cols_ + col];
}

void ErrorConcealmentMask::set(uint32_t col, uint32_t row, BlockStatus status) noexcept {
    if (col < cols_ && row < rows_) {
        grid_[static_cast<size_t>(row) * cols_ + col] = status;
    }
}

void ErrorConcealmentMask::mark_damaged(uint32_t col, uint32_t row) noexcept {
    set(col, row, BlockStatus::Damaged);
}

void ErrorConcealmentMask::mark_damaged_range(uint32_t start_col, uint32_t start_row,
                                             uint32_t end_col, uint32_t end_row) noexcept {
    start_col = std::min(start_col, cols_);
    start_row = std::min(start_row, rows_);
    end_col = std::min(end_col, cols_);
    end_row = std::min(end_row, rows_);

    for (uint32_t r = start_row; r < end_row; ++r) {
        for (uint32_t c = start_col; c < end_col; ++c) {
            grid_[static_cast<size_t>(r) * cols_ + c] = BlockStatus::Damaged;
        }
    }
}

void ErrorConcealmentMask::mark_damaged_pixel_rect(uint32_t px, uint32_t py, uint32_t pw, uint32_t ph) noexcept {
    if (block_size_ == 0) return;
    uint32_t sc = px / block_size_;
    uint32_t sr = py / block_size_;
    uint32_t ec = (px + pw + block_size_ - 1) / block_size_;
    uint32_t er = (py + ph + block_size_ - 1) / block_size_;
    mark_damaged_range(sc, sr, ec, er);
}

bool ErrorConcealmentMask::is_damaged(uint32_t col, uint32_t row) const noexcept {
    return get(col, row) == BlockStatus::Damaged;
}

bool ErrorConcealmentMask::is_valid(uint32_t col, uint32_t row) const noexcept {
    auto s = get(col, row);
    return s != BlockStatus::Damaged;
}

bool ErrorConcealmentMask::has_damaged_blocks() const noexcept {
    for (auto s : grid_) {
        if (s == BlockStatus::Damaged) return true;
    }
    return false;
}

size_t ErrorConcealmentMask::damaged_count() const noexcept {
    size_t count = 0;
    for (auto s : grid_) {
        if (s == BlockStatus::Damaged) count++;
    }
    return count;
}

float ErrorConcealmentMask::damage_ratio() const noexcept {
    if (grid_.empty()) return 0.0f;
    return static_cast<float>(damaged_count()) / static_cast<float>(grid_.size());
}

bool ErrorConcealmentMask::has_valid_neighbor(uint32_t col, uint32_t row) const noexcept {
    // Check Left
    if (col > 0 && is_valid(col - 1, row)) return true;
    // Check Right
    if (col + 1 < cols_ && is_valid(col + 1, row)) return true;
    // Check Top
    if (row > 0 && is_valid(col, row - 1)) return true;
    // Check Bottom
    if (row + 1 < rows_ && is_valid(col, row + 1)) return true;

    return false;
}

BlockStatus ErrorConcealmentMask::neighbor_status(uint32_t col, uint32_t row, int dx, int dy) const noexcept {
    int nc = static_cast<int>(col) + dx;
    int nr = static_cast<int>(row) + dy;
    if (nc < 0 || nc >= static_cast<int>(cols_) || nr < 0 || nr >= static_cast<int>(rows_)) {
        return BlockStatus::Valid; // Out of bounds treated as neutral boundary
    }
    return get(static_cast<uint32_t>(nc), static_cast<uint32_t>(nr));
}

std::optional<ErrorConcealmentMask::BoundingBox> ErrorConcealmentMask::damaged_bounding_box() const noexcept {
    if (!has_damaged_blocks()) return std::nullopt;

    BoundingBox bbox{cols_, rows_, 0, 0};
    for (uint32_t r = 0; r < rows_; ++r) {
        for (uint32_t c = 0; c < cols_; ++c) {
            if (is_damaged(c, r)) {
                bbox.min_col = std::min(bbox.min_col, c);
                bbox.min_row = std::min(bbox.min_row, r);
                bbox.max_col = std::max(bbox.max_col, c);
                bbox.max_row = std::max(bbox.max_row, r);
            }
        }
    }
    return bbox;
}

void ErrorConcealmentMask::clear(BlockStatus status) noexcept {
    std::fill(grid_.begin(), grid_.end(), status);
}

} // namespace atum
