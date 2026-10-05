#include "atum/color/transfer_function.hpp"

namespace atum {

float TransferFunction::to_linear(float non_linear, TransferCharacteristic transfer) noexcept {
    switch (transfer) {
        case TransferCharacteristic::SMPTE2084:
            return pq_eotf(non_linear);
        case TransferCharacteristic::ARIB_STD_B67:
            return hlg_inverse_oetf(non_linear);
        case TransferCharacteristic::BT709:
        case TransferCharacteristic::SMPTE170M:
        case TransferCharacteristic::BT2020_10:
        case TransferCharacteristic::BT2020_12:
            return bt709_inverse_oetf(non_linear);
        case TransferCharacteristic::IEC61966_2_1:
            return srgb_inverse_oetf(non_linear);
        case TransferCharacteristic::Linear:
            return std::clamp(non_linear, 0.0f, 1.0f);
        case TransferCharacteristic::Gamma22:
            return std::pow(std::clamp(non_linear, 0.0f, 1.0f), 2.2f);
        case TransferCharacteristic::Gamma28:
            return std::pow(std::clamp(non_linear, 0.0f, 1.0f), 2.8f);
        case TransferCharacteristic::Unspecified:
        default:
            return bt709_inverse_oetf(non_linear);
    }
}

float TransferFunction::from_linear(float linear_val, TransferCharacteristic transfer) noexcept {
    switch (transfer) {
        case TransferCharacteristic::SMPTE2084:
            return pq_oetf(linear_val);
        case TransferCharacteristic::ARIB_STD_B67:
            return hlg_oetf(linear_val);
        case TransferCharacteristic::BT709:
        case TransferCharacteristic::SMPTE170M:
        case TransferCharacteristic::BT2020_10:
        case TransferCharacteristic::BT2020_12:
            return bt709_oetf(linear_val);
        case TransferCharacteristic::IEC61966_2_1:
            return srgb_oetf(linear_val);
        case TransferCharacteristic::Linear:
            return std::clamp(linear_val, 0.0f, 1.0f);
        case TransferCharacteristic::Gamma22:
            return std::pow(std::clamp(linear_val, 0.0f, 1.0f), 1.0f / 2.2f);
        case TransferCharacteristic::Gamma28:
            return std::pow(std::clamp(linear_val, 0.0f, 1.0f), 1.0f / 2.8f);
        case TransferCharacteristic::Unspecified:
        default:
            return bt709_oetf(linear_val);
    }
}

TransferFunctionLUT TransferFunctionLUT::create_pq_eotf() {
    TransferFunctionLUT lut;
    lut.initialize([](float x) { return TransferFunction::pq_eotf(x); });
    return lut;
}

TransferFunctionLUT TransferFunctionLUT::create_pq_oetf() {
    TransferFunctionLUT lut;
    lut.initialize([](float x) { return TransferFunction::pq_oetf(x); });
    return lut;
}

TransferFunctionLUT TransferFunctionLUT::create_hlg_inverse_oetf() {
    TransferFunctionLUT lut;
    lut.initialize([](float x) { return TransferFunction::hlg_inverse_oetf(x); });
    return lut;
}

TransferFunctionLUT TransferFunctionLUT::create_bt709_inverse_oetf() {
    TransferFunctionLUT lut;
    lut.initialize([](float x) { return TransferFunction::bt709_inverse_oetf(x); });
    return lut;
}

TransferFunctionLUT TransferFunctionLUT::create_srgb_inverse_oetf() {
    TransferFunctionLUT lut;
    lut.initialize([](float x) { return TransferFunction::srgb_inverse_oetf(x); });
    return lut;
}

void TransferFunctionLUT::evaluate_batch(const float* __restrict src, float* __restrict dst, size_t count) const noexcept {
    const float max_idx = static_cast<float>(TABLE_SIZE - 1);
    for (size_t i = 0; i < count; ++i) {
        float x = std::clamp(src[i], 0.0f, 1.0f);
        float pos = x * max_idx;
        size_t idx = static_cast<size_t>(pos);
        if (idx >= TABLE_SIZE - 1) {
            dst[i] = table_[TABLE_SIZE - 1];
        } else {
            float frac = pos - static_cast<float>(idx);
            dst[i] = table_[idx] * (1.0f - frac) + table_[idx + 1] * frac;
        }
    }
}

} // namespace atum
