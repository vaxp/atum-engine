#include "atum/predict/chroma_pred.hpp"

namespace atum {

void ChromaPredictor::downsample_luma(
    const int16_t* luma, size_t luma_stride,
    int16_t* dst_downsampled,
    uint32_t width, uint32_t height,
    ChromaSubsampling subsampling
) noexcept {
    if (!luma || !dst_downsampled) return;

    if (subsampling == ChromaSubsampling::CS_420) {
        for (uint32_t y = 0; y < height; ++y) {
            const int16_t* r0 = luma + (y * 2) * luma_stride;
            const int16_t* r1 = luma + (y * 2 + 1) * luma_stride;
            int16_t* pdst = dst_downsampled + y * width;

            for (uint32_t x = 0; x < width; ++x) {
                const int32_t sum = r0[x * 2] + r0[x * 2 + 1] + r1[x * 2] + r1[x * 2 + 1];
                pdst[x] = static_cast<int16_t>((sum + 2) >> 2);
            }
        }
    } else if (subsampling == ChromaSubsampling::CS_422) {
        for (uint32_t y = 0; y < height; ++y) {
            const int16_t* row = luma + y * luma_stride;
            int16_t* pdst = dst_downsampled + y * width;

            for (uint32_t x = 0; x < width; ++x) {
                const int32_t sum = row[x * 2] + row[x * 2 + 1];
                pdst[x] = static_cast<int16_t>((sum + 1) >> 1);
            }
        }
    } else { // 4:4:4
        for (uint32_t y = 0; y < height; ++y) {
            const int16_t* row = luma + y * luma_stride;
            int16_t* pdst = dst_downsampled + y * width;

            for (uint32_t x = 0; x < width; ++x) {
                pdst[x] = row[x];
            }
        }
    }
}

void ChromaPredictor::compute_ac_luma(
    const int16_t* luma_downsampled,
    int16_t* dst_ac_luma,
    uint32_t width, uint32_t height
) noexcept {
    if (!luma_downsampled || !dst_ac_luma || width == 0 || height == 0) return;

    const size_t total_samples = static_cast<size_t>(width) * height;
    int64_t sum = 0;
    for (size_t i = 0; i < total_samples; ++i) {
        sum += luma_downsampled[i];
    }

    const int16_t mean = static_cast<int16_t>((sum + static_cast<int64_t>(total_samples / 2)) / total_samples);

    for (size_t i = 0; i < total_samples; ++i) {
        dst_ac_luma[i] = static_cast<int16_t>(luma_downsampled[i] - mean);
    }
}

void ChromaPredictor::predict_cfl(
    const int16_t* base_pred,
    const int16_t* ac_luma,
    int16_t* dst_pred,
    uint32_t width, uint32_t height,
    int16_t alpha_q4,
    uint8_t bit_depth
) noexcept {
    if (!base_pred || !ac_luma || !dst_pred) return;

    const size_t total_samples = static_cast<size_t>(width) * height;
    const int32_t max_val = (1 << bit_depth) - 1;

    for (size_t i = 0; i < total_samples; ++i) {
        const int32_t delta = (static_cast<int32_t>(alpha_q4) * static_cast<int32_t>(ac_luma[i]) + 8) >> 4;
        const int32_t val = static_cast<int32_t>(base_pred[i]) + delta;
        dst_pred[i] = static_cast<int16_t>(std::clamp(val, 0, max_val));
    }
}

void ChromaPredictor::pred_422_dc(
    const int16_t* top_neighbors,
    const int16_t* left_neighbors,
    int16_t* dst, size_t dst_stride,
    uint32_t width, uint32_t height
) noexcept {
    if (!top_neighbors || !left_neighbors || !dst || width == 0 || height == 0) return;

    int32_t sum = 0;
    for (uint32_t x = 0; x < width; ++x) {
        sum += top_neighbors[x];
    }
    for (uint32_t y = 0; y < height; ++y) {
        sum += left_neighbors[y];
    }

    const int16_t dc = static_cast<int16_t>((sum + static_cast<int32_t>((width + height) / 2)) / (width + height));

    for (uint32_t y = 0; y < height; ++y) {
        int16_t* row = dst + y * dst_stride;
        for (uint32_t x = 0; x < width; ++x) {
            row[x] = dc;
        }
    }
}

void ChromaPredictor::pred_422_planar(
    const int16_t* top_neighbors,
    const int16_t* left_neighbors,
    int16_t* dst, size_t dst_stride,
    uint32_t width, uint32_t height
) noexcept {
    if (!top_neighbors || !left_neighbors || !dst || width == 0 || height == 0) return;

    const int32_t tr = top_neighbors[width - 1];
    const int32_t bl = left_neighbors[height - 1];
    const int32_t log2_w = 31 - __builtin_clz(width);
    const int32_t log2_h = 31 - __builtin_clz(height);

    for (uint32_t y = 0; y < height; ++y) {
        int16_t* row = dst + y * dst_stride;
        const int32_t ly = left_neighbors[y];

        for (uint32_t x = 0; x < width; ++x) {
            const int32_t tx = top_neighbors[x];
            // Bilinear interpolation across rectangular boundary
            const int32_t horiz = (static_cast<int32_t>(width - 1 - x) * ly + static_cast<int32_t>(x + 1) * tr);
            const int32_t vert  = (static_cast<int32_t>(height - 1 - y) * tx + static_cast<int32_t>(y + 1) * bl);
            const int32_t val = (horiz << log2_h) + (vert << log2_w) + (1 << (log2_w + log2_h));
            row[x] = static_cast<int16_t>(val >> (log2_w + log2_h + 1));
        }
    }
}

void ChromaPredictor::pred_422_vertical(
    const int16_t* top_neighbors,
    int16_t* dst, size_t dst_stride,
    uint32_t width, uint32_t height
) noexcept {
    if (!top_neighbors || !dst) return;

    for (uint32_t y = 0; y < height; ++y) {
        int16_t* row = dst + y * dst_stride;
        for (uint32_t x = 0; x < width; ++x) {
            row[x] = top_neighbors[x];
        }
    }
}

void ChromaPredictor::pred_422_horizontal(
    const int16_t* left_neighbors,
    int16_t* dst, size_t dst_stride,
    uint32_t width, uint32_t height
) noexcept {
    if (!left_neighbors || !dst) return;

    for (uint32_t y = 0; y < height; ++y) {
        int16_t* row = dst + y * dst_stride;
        const int16_t val = left_neighbors[y];
        for (uint32_t x = 0; x < width; ++x) {
            row[x] = val;
        }
    }
}

void ChromaPredictor::predict_residual_rext(
    const int16_t* luma_residual, size_t luma_stride,
    int16_t* chroma_residual, size_t chroma_stride,
    uint32_t width, uint32_t height,
    int16_t alpha, uint8_t shift
) noexcept {
    if (!luma_residual || !chroma_residual) return;

    for (uint32_t y = 0; y < height; ++y) {
        const int16_t* r_luma = luma_residual + y * luma_stride;
        int16_t* r_chroma = chroma_residual + y * chroma_stride;

        for (uint32_t x = 0; x < width; ++x) {
            const int32_t delta = (static_cast<int32_t>(alpha) * static_cast<int32_t>(r_luma[x])) >> shift;
            r_chroma[x] = static_cast<int16_t>(r_chroma[x] + delta);
        }
    }
}

} // namespace atum
