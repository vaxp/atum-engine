#include "atum/codec/screen/screen_content.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

namespace atum {

PaletteAnalysis ScreenContent::analyze_palette(
    const uint8_t* block, size_t stride,
    uint32_t width, uint32_t height,
    uint32_t max_palette_size
) noexcept {
    PaletteAnalysis result;
    if (!block || width == 0 || height == 0) return result;

    result.indices.resize(width * height);

    // Map unique pixel values to palette indices
    std::vector<uint32_t> unique_colors;
    unique_colors.reserve(max_palette_size + 1);

    std::array<uint32_t, 256> hist{};
    uint32_t total_pixels = width * height;

    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* row = block + y * stride;
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t val = row[x];
            hist[val & 0xFF]++;

            auto it = std::find(unique_colors.begin(), unique_colors.end(), val);
            size_t idx = 0;
            if (it == unique_colors.end()) {
                if (unique_colors.size() < max_palette_size) {
                    idx = unique_colors.size();
                    unique_colors.push_back(val);
                } else {
                    // Exceeded max palette size: not a pure flat palette block
                    result.is_palette = false;
                    result.num_unique_colors = static_cast<uint32_t>(unique_colors.size() + 1);
                    return result;
                }
            } else {
                idx = std::distance(unique_colors.begin(), it);
            }
            result.indices[y * width + x] = static_cast<uint8_t>(idx);
        }
    }

    result.is_palette = true;
    result.colors = std::move(unique_colors);
    result.num_unique_colors = static_cast<uint32_t>(result.colors.size());

    // Calculate Shannon entropy
    double entropy = 0.0;
    for (uint32_t c : result.colors) {
        uint32_t count = hist[c & 0xFF];
        if (count > 0) {
            double p = static_cast<double>(count) / total_pixels;
            entropy -= p * std::log2(p);
        }
    }
    result.estimated_entropy = entropy;

    return result;
}

IbcMatch ScreenContent::search_intrablock_copy(
    const uint8_t* cur_block, size_t cur_stride,
    const uint8_t* recon_plane, size_t recon_stride,
    int32_t cur_x, int32_t cur_y,
    int32_t frame_width, int32_t frame_height,
    uint32_t block_w, uint32_t block_h,
    int32_t search_distance
) noexcept {
    IbcMatch best_match;
    if (!cur_block || !recon_plane) return best_match;

    int32_t bw = static_cast<int32_t>(block_w);
    int32_t bh = static_cast<int32_t>(block_h);

    int32_t min_x = std::max(0, cur_x - search_distance);
    int32_t max_x = std::min(frame_width - bw, cur_x + search_distance);
    int32_t min_y = std::max(0, cur_y - search_distance);
    int32_t max_y = std::min(cur_y, frame_height - bh); // Must be in current or preceding rows and within frame

    for (int32_t ry = min_y; ry <= max_y; ry += 4) {
        for (int32_t rx = min_x; rx <= max_x; rx += 4) {
            // Must strictly precede current block in raster order to be available in reconstructed area
            if (ry == cur_y && rx + bw > cur_x) {
                continue;
            }

            // Calculate SAD
            uint32_t sad = 0;
            for (int32_t y = 0; y < bh; ++y) {
                const uint8_t* cb = cur_block + y * cur_stride;
                const uint8_t* rb = recon_plane + (ry + y) * recon_stride + rx;
                for (int32_t x = 0; x < bw; ++x) {
                    sad += std::abs(static_cast<int32_t>(cb[x]) - static_cast<int32_t>(rb[x]));
                }
                if (sad >= best_match.distortion) break;
            }

            if (sad < best_match.distortion) {
                best_match.found = true;
                best_match.distortion = sad;
                best_match.bv_x = rx - cur_x;
                best_match.bv_y = ry - cur_y;

                if (sad == 0) {
                    // Exact clone found, early exit
                    return best_match;
                }
            }
        }
    }

    return best_match;
}

} // namespace atum
