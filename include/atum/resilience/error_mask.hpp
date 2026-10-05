#pragma once

#include <cstdint>
#include <vector>
#include <optional>
#include <algorithm>

namespace atum {

/// Concealment state of a macroblock or CTU.
enum class BlockStatus : uint8_t {
    Valid = 0,             ///< Undamaged macroblock with valid decoded bitstream
    Damaged = 1,           ///< Corrupted or lost macroblock requiring concealment
    ConcealedSpatial = 2,  ///< Reconstructed using spatial boundary interpolation
    ConcealedTemporal = 3  ///< Reconstructed using temporal motion compensation
};

/// 2D Macroblock / CTU status grid tracking damage across video frames.
class ErrorConcealmentMask {
public:
    struct BoundingBox {
        uint32_t min_col{0};
        uint32_t min_row{0};
        uint32_t max_col{0};
        uint32_t max_row{0};
    };

    ErrorConcealmentMask() = default;
    ErrorConcealmentMask(uint32_t width, uint32_t height, uint32_t block_size = 16);

    void reset(uint32_t width, uint32_t height, uint32_t block_size = 16);

    [[nodiscard]] uint32_t width() const noexcept { return width_; }
    [[nodiscard]] uint32_t height() const noexcept { return height_; }
    [[nodiscard]] uint32_t block_size() const noexcept { return block_size_; }
    [[nodiscard]] uint32_t cols() const noexcept { return cols_; }
    [[nodiscard]] uint32_t rows() const noexcept { return rows_; }
    [[nodiscard]] size_t total_blocks() const noexcept { return static_cast<size_t>(cols_) * rows_; }

    [[nodiscard]] BlockStatus get(uint32_t col, uint32_t row) const noexcept;
    void set(uint32_t col, uint32_t row, BlockStatus status) noexcept;

    void mark_damaged(uint32_t col, uint32_t row) noexcept;
    void mark_damaged_range(uint32_t start_col, uint32_t start_row, uint32_t end_col, uint32_t end_row) noexcept;
    void mark_damaged_pixel_rect(uint32_t px, uint32_t py, uint32_t pw, uint32_t ph) noexcept;

    [[nodiscard]] bool is_damaged(uint32_t col, uint32_t row) const noexcept;
    [[nodiscard]] bool is_valid(uint32_t col, uint32_t row) const noexcept;

    [[nodiscard]] bool has_damaged_blocks() const noexcept;
    [[nodiscard]] size_t damaged_count() const noexcept;
    [[nodiscard]] float damage_ratio() const noexcept;

    [[nodiscard]] bool has_valid_neighbor(uint32_t col, uint32_t row) const noexcept;
    [[nodiscard]] BlockStatus neighbor_status(uint32_t col, uint32_t row, int dx, int dy) const noexcept;

    [[nodiscard]] std::optional<BoundingBox> damaged_bounding_box() const noexcept;

    void clear(BlockStatus status = BlockStatus::Valid) noexcept;

private:
    uint32_t width_{0};
    uint32_t height_{0};
    uint32_t block_size_{16};
    uint32_t cols_{0};
    uint32_t rows_{0};
    std::vector<BlockStatus> grid_;
};

} // namespace atum
