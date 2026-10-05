#include "atum/inloop/sao_hevc.hpp"
#include <algorithm>

namespace atum {

namespace {

inline int32_t sgn(int32_t val) noexcept {
    return (val > 0) - (val < 0);
}

} // anonymous namespace

void SaoHevc::apply_sao(
    uint8_t* pix,
    size_t stride,
    uint32_t width,
    uint32_t height,
    const SaoParams& params,
    int32_t bit_depth
) noexcept {
    if (params.type == SaoType::None || width == 0 || height == 0) return;

    int32_t max_val = (1 << bit_depth) - 1;

    if (params.type == SaoType::BandOffset) {
        int32_t start_band = params.band_position;
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                int32_t sample = pix[y * stride + x];
                int32_t band = sample >> 3; // 32 uniform bands
                int32_t band_idx = band - start_band;
                if (band_idx >= 0 && band_idx < 4) {
                    pix[y * stride + x] = static_cast<uint8_t>(std::clamp(sample + params.offsets[band_idx], 0, max_val));
                }
            }
        }
        return;
    }

    // Edge Offset (0, 90, 135, 45 degrees)
    int32_t dx0 = 0, dy0 = 0;
    int32_t dx1 = 0, dy1 = 0;

    switch (params.type) {
        case SaoType::EdgeOffset0:   // Horizontal (0 deg)
            dx0 = -1; dy0 = 0;
            dx1 = 1;  dy1 = 0;
            break;
        case SaoType::EdgeOffset90:  // Vertical (90 deg)
            dx0 = 0; dy0 = -1;
            dx1 = 0; dy1 = 1;
            break;
        case SaoType::EdgeOffset135: // Diagonal (135 deg)
            dx0 = -1; dy0 = -1;
            dx1 = 1;  dy1 = 1;
            break;
        case SaoType::EdgeOffset45:  // Diagonal (45 deg)
            dx0 = 1;  dy0 = -1;
            dx1 = -1; dy1 = 1;
            break;
        default:
            return;
    }

    for (uint32_t y = 1; y + 1 < height; ++y) {
        for (uint32_t x = 1; x + 1 < width; ++x) {
            int32_t p  = pix[y * stride + x];
            int32_t p0 = pix[(y + dy0) * stride + (x + dx0)];
            int32_t p1 = pix[(y + dy1) * stride + (x + dx1)];

            int32_t edge_idx = sgn(p - p0) + sgn(p - p1) + 2; // Range 0..4

            int32_t offset = 0;
            if (edge_idx == 0) offset = params.offsets[0];      // Valley
            else if (edge_idx == 1) offset = params.offsets[1]; // Half-valley
            else if (edge_idx == 3) offset = -params.offsets[2];// Half-peak
            else if (edge_idx == 4) offset = -params.offsets[3];// Peak

            if (offset != 0) {
                pix[y * stride + x] = static_cast<uint8_t>(std::clamp(p + offset, 0, max_val));
            }
        }
    }
}

} // namespace atum
