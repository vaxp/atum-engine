#include "atum/color/tone_mapper.hpp"

namespace atum {

void ToneMapper::get_luminance_weights(float& kr, float& kg, float& kb) const noexcept {
    switch (primaries) {
        case ColorPrimaries::BT2020:
            kr = 0.2627f;
            kg = 0.6780f;
            kb = 0.0593f;
            break;
        case ColorPrimaries::DCI_P3:
        case ColorPrimaries::Display_P3:
            kr = 0.2093f;
            kg = 0.7216f;
            kb = 0.0691f;
            break;
        case ColorPrimaries::BT709:
        case ColorPrimaries::BT470BG:
        case ColorPrimaries::SMPTE170M:
        default:
            kr = 0.2126f;
            kg = 0.7152f;
            kb = 0.0722f;
            break;
    }
}

void ToneMapper::map_rgb(float r, float g, float b, float& out_r, float& out_g, float& out_b) const noexcept {
    if (op == ToneMappingOperator::NONE) {
        out_r = std::clamp(r * exposure, 0.0f, 1.0f);
        out_g = std::clamp(g * exposure, 0.0f, 1.0f);
        out_b = std::clamp(b * exposure, 0.0f, 1.0f);
        return;
    }

    if (preserve_chromaticity) {
        float kr, kg, kb;
        get_luminance_weights(kr, kg, kb);
        const float y_in = kr * r + kg * g + kb * b;
        if (y_in <= 1e-6f) {
            out_r = 0.0f;
            out_g = 0.0f;
            out_b = 0.0f;
            return;
        }

        const float y_out = evaluate_curve(y_in);
        const float scale = y_out / y_in;

        const float rs = r * scale;
        const float gs = g * scale;
        const float bs = b * scale;
        const float norm_max = std::max({rs, gs, bs, 1.0f});

        out_r = rs / norm_max;
        out_g = gs / norm_max;
        out_b = bs / norm_max;
    } else {
        out_r = evaluate_curve(r);
        out_g = evaluate_curve(g);
        out_b = evaluate_curve(b);
    }
}

void ToneMapper::map_batch(const float* __restrict r_in,
                           const float* __restrict g_in,
                           const float* __restrict b_in,
                           float* __restrict r_out,
                           float* __restrict g_out,
                           float* __restrict b_out,
                           size_t count) const noexcept {
    float kr, kg, kb;
    get_luminance_weights(kr, kg, kb);

    for (size_t i = 0; i < count; ++i) {
        const float r = r_in[i];
        const float g = g_in[i];
        const float b = b_in[i];

        if (op == ToneMappingOperator::NONE) {
            r_out[i] = std::clamp(r * exposure, 0.0f, 1.0f);
            g_out[i] = std::clamp(g * exposure, 0.0f, 1.0f);
            b_out[i] = std::clamp(b * exposure, 0.0f, 1.0f);
        } else if (preserve_chromaticity) {
            const float y_in = kr * r + kg * g + kb * b;
            if (y_in <= 1e-6f) {
                r_out[i] = 0.0f;
                g_out[i] = 0.0f;
                b_out[i] = 0.0f;
            } else {
                const float y_out = evaluate_curve(y_in);
                const float scale = y_out / y_in;

                const float rs = r * scale;
                const float gs = g * scale;
                const float bs = b * scale;
                const float norm_max = std::max({rs, gs, bs, 1.0f});

                r_out[i] = rs / norm_max;
                g_out[i] = gs / norm_max;
                b_out[i] = bs / norm_max;
            }
        } else {
            r_out[i] = evaluate_curve(r);
            g_out[i] = evaluate_curve(g);
            b_out[i] = evaluate_curve(b);
        }
    }
}

void ToneMapper::map_rgba_inplace(float* __restrict rgba, size_t pixel_count) const noexcept {
    float kr, kg, kb;
    get_luminance_weights(kr, kg, kb);

    for (size_t i = 0; i < pixel_count; ++i) {
        float* p = rgba + i * 4;
        const float r = p[0];
        const float g = p[1];
        const float b = p[2];

        if (op == ToneMappingOperator::NONE) {
            p[0] = std::clamp(r * exposure, 0.0f, 1.0f);
            p[1] = std::clamp(g * exposure, 0.0f, 1.0f);
            p[2] = std::clamp(b * exposure, 0.0f, 1.0f);
        } else if (preserve_chromaticity) {
            const float y_in = kr * r + kg * g + kb * b;
            if (y_in <= 1e-6f) {
                p[0] = 0.0f;
                p[1] = 0.0f;
                p[2] = 0.0f;
            } else {
                const float y_out = evaluate_curve(y_in);
                const float scale = y_out / y_in;

                const float rs = r * scale;
                const float gs = g * scale;
                const float bs = b * scale;
                const float norm_max = std::max({rs, gs, bs, 1.0f});

                p[0] = rs / norm_max;
                p[1] = gs / norm_max;
                p[2] = bs / norm_max;
            }
        } else {
            p[0] = evaluate_curve(r);
            p[1] = evaluate_curve(g);
            p[2] = evaluate_curve(b);
        }
        // Alpha untouched
    }
}

} // namespace atum
