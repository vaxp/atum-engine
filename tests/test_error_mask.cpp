#include "atum/resilience/error_mask.hpp"
#include <cassert>
#include <iostream>
#include <cmath>

using namespace atum;

static void test_initialization() {
    ErrorConcealmentMask mask(1920, 1080, 16);
    assert(mask.width() == 1920);
    assert(mask.height() == 1080);
    assert(mask.block_size() == 16);
    assert(mask.cols() == 120);
    assert(mask.rows() == 68);
    assert(mask.total_blocks() == 120 * 68);
    assert(!mask.has_damaged_blocks());
    assert(mask.damaged_count() == 0);
    assert(mask.damage_ratio() == 0.0f);
    assert(!mask.damaged_bounding_box().has_value());

    for (uint32_t r = 0; r < mask.rows(); ++r) {
        for (uint32_t c = 0; c < mask.cols(); ++c) {
            assert(mask.get(c, r) == BlockStatus::Valid);
            assert(mask.is_valid(c, r));
            assert(!mask.is_damaged(c, r));
        }
    }
    std::cout << "test_initialization passed.\n";
}

static void test_single_block_marking() {
    ErrorConcealmentMask mask(64, 64, 16);
    assert(mask.cols() == 4);
    assert(mask.rows() == 4);

    mask.mark_damaged(1, 2);
    assert(mask.is_damaged(1, 2));
    assert(mask.get(1, 2) == BlockStatus::Damaged);
    assert(mask.has_damaged_blocks());
    assert(mask.damaged_count() == 1);
    assert(std::abs(mask.damage_ratio() - (1.0f / 16.0f)) < 1e-4f);

    auto bbox = mask.damaged_bounding_box();
    assert(bbox.has_value());
    assert(bbox->min_col == 1 && bbox->max_col == 1);
    assert(bbox->min_row == 2 && bbox->max_row == 2);

    assert(mask.has_valid_neighbor(1, 2));
    assert(mask.neighbor_status(1, 2, 0, -1) == BlockStatus::Valid); // top
    assert(mask.neighbor_status(1, 2, 0, 1) == BlockStatus::Valid);  // bottom

    // Out of bounds check
    assert(mask.neighbor_status(0, 0, -1, 0) == BlockStatus::Valid); // outside treated as neutral/valid boundary
    std::cout << "test_single_block_marking passed.\n";
}

static void test_range_and_pixel_rect() {
    ErrorConcealmentMask mask(320, 240, 16); // 20 cols x 15 rows = 300 blocks
    assert(mask.cols() == 20 && mask.rows() == 15);

    // Range mark [2..5) x [3..6) => 3 cols x 3 rows = 9 blocks
    mask.mark_damaged_range(2, 3, 5, 6);
    assert(mask.damaged_count() == 9);

    auto bbox = mask.damaged_bounding_box();
    assert(bbox.has_value());
    assert(bbox->min_col == 2 && bbox->max_col == 4);
    assert(bbox->min_row == 3 && bbox->max_row == 5);

    // Mark pixel rectangle
    // px=160, py=120, w=32, h=32 => col 10..11, row 7..8
    mask.mark_damaged_pixel_rect(160, 120, 32, 32);
    assert(mask.is_damaged(10, 7));
    assert(mask.is_damaged(11, 8));

    mask.clear(BlockStatus::Valid);
    assert(!mask.has_damaged_blocks());
    assert(mask.damaged_count() == 0);
    std::cout << "test_range_and_pixel_rect passed.\n";
}

static void test_concealed_statuses() {
    ErrorConcealmentMask mask(64, 64, 16);
    mask.set(0, 0, BlockStatus::Damaged);
    mask.set(1, 0, BlockStatus::ConcealedSpatial);
    mask.set(2, 0, BlockStatus::ConcealedTemporal);

    assert(mask.is_damaged(0, 0));
    assert(!mask.is_damaged(1, 0));
    assert(!mask.is_damaged(2, 0));
    assert(mask.is_valid(1, 0)); // concealed counts as restored/valid for neighboring
    assert(mask.is_valid(2, 0));
    assert(mask.damaged_count() == 1);
    std::cout << "test_concealed_statuses passed.\n";
}

int main() {
    std::cout << "Running test_error_mask...\n";
    test_initialization();
    test_single_block_marking();
    test_range_and_pixel_rect();
    test_concealed_statuses();
    std::cout << "ALL test_error_mask checks passed successfully!\n";
    return 0;
}
