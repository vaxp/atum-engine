#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <span>
#include <array>

namespace atum {

/// Palette mode analysis for synthetic computer graphics and UI.
struct PaletteAnalysis {
    bool is_palette{false};
    std::vector<uint32_t> colors;       // Unique RGB or YUV colors (up to max_palette_size)
    std::vector<uint8_t> indices;       // Index per pixel into `colors`
    uint32_t num_unique_colors{0};
    double estimated_entropy{0.0};
};

/// Intra Block Copy (IntraBC) match candidate within the reconstructed frame area.
struct IbcMatch {
    bool found{false};
    int32_t bv_x{0};                    // Block displacement vector X
    int32_t bv_y{0};                    // Block displacement vector Y
    uint32_t distortion{UINT32_MAX};    // SAD distortion (0 = bit-exact match)
};

class ScreenContent {
public:
    /// Analyzes an 8x8 or 16x16 pixel block for palette mode suitability.
    /// Synthetic canvas drawings with flat colors, text, and sharp boundaries
    /// typically contain <= 8 unique colors.
    static PaletteAnalysis analyze_palette(
        const uint8_t* block, size_t stride,
        uint32_t width, uint32_t height,
        uint32_t max_palette_size = 8
    ) noexcept;

    /// Searches previously reconstructed macroblocks of the current frame for matching patterns.
    /// `cur_x`, `cur_y` are the current block coordinates.
    /// Only areas strictly preceding the current block in raster order are valid reference candidates.
    static IbcMatch search_intrablock_copy(
        const uint8_t* cur_block, size_t cur_stride,
        const uint8_t* recon_plane, size_t recon_stride,
        int32_t cur_x, int32_t cur_y,
        int32_t frame_width, int32_t frame_height,
        uint32_t block_w = 16, uint32_t block_h = 16,
        int32_t search_distance = 64
    ) noexcept;
};

} // namespace atum
