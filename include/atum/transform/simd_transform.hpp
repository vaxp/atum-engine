#pragma once

#include <cstdint>
#include <cstddef>
#include <span>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  #if defined(__SSE2__) || defined(_M_X64)
    #define NISABA_MEDIA_HAS_SSE2 1
    #include <immintrin.h>
  #endif
  #if defined(__AVX2__)
    #define NISABA_MEDIA_HAS_AVX2 1
  #endif
#endif

namespace atum::simd {

/// Vectorized 4x4 H.264 Core Inverse Transform using SSE2 / AVX2.
/// Computes both horizontal and vertical butterfly passes with 100% bit-exact parity to reference.
void idct4x4_h264(int32_t* block) noexcept;

/// Vectorized 4x4 H.264 Core Inverse Transform and add residual to prediction buffer.
void idct4x4_h264_add(
    const int32_t* residual,
    const uint8_t* pred,
    uint8_t* dst,
    size_t dst_stride,
    size_t pred_stride = 0
) noexcept;

/// Vectorized 8x8 H.264 Core Inverse Transform using SSE2 / AVX2.
void idct8x8_h264(int32_t* block) noexcept;

} // namespace atum::simd
