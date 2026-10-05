#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>

namespace atum {

/// Transform block dimensions.
enum class TransformSize : uint8_t {
    Tx4x4,
    Tx8x8,
    Tx16x16,
    Tx32x32,
};

constexpr uint32_t transform_size_to_pixels(TransformSize size) noexcept {
    switch (size) {
        case TransformSize::Tx4x4:   return 4;
        case TransformSize::Tx8x8:   return 8;
        case TransformSize::Tx16x16: return 16;
        case TransformSize::Tx32x32: return 32;
    }
    return 4;
}

/// Transform mathematical family.
enum class TransformType : uint8_t {
    DCT,      ///< Discrete Cosine Transform (Type II integer approximation)
    DST,      ///< Discrete Sine Transform (Type VII for intra prediction residuals)
    ADST,     ///< Asymmetric Discrete Sine Transform (VP9 / AV1)
    WHT,      ///< Walsh-Hadamard Transform (Lossless / DC blocks)
    Identity  ///< Identity / Pass-through transform
};

} // namespace atum
