#pragma once

#include <cstdint>
#include <vector>
#include <optional>
#include <span>
#include "atum/core/media_types.hpp"
#include "atum/codec/video/h264/h264_sps.hpp"
#include "atum/codec/video/h264/h264_pps.hpp"
#include "atum/codec/video/h264/h264_slice.hpp"
#include "atum/codec/video/hevc/hevc_parameter_sets.hpp"
#include "atum/codec/video/hevc/hevc_slice.hpp"
#include "atum/codec/video/vp9/vp9_header.hpp"
#include "atum/codec/video/av1/av1_obu.hpp"
#include "atum/predict/dpb.hpp"

#if __has_include(<va/va.h>)
#include <va/va.h>
#include <va/va_dec_hevc.h>
#include <va/va_dec_vp9.h>
#include <va/va_dec_av1.h>
#include <va/va_enc_h264.h>
#include <va/va_enc_hevc.h>
#define ATUM_HAS_VAAPI_HEADERS 1
#endif

namespace atum {

#if defined(ATUM_HAS_VAAPI_HEADERS)

/// Sovereign VA-API H.264 Parameter Packet for GPU Decode submission
struct VaapiH264DecodeParams {
    VAPictureParameterBufferH264 pic_param{};
    VAIQMatrixBufferH264 iq_matrix{};
    VASliceParameterBufferH264 slice_param{};
    bool has_slice{false};

    static VaapiH264DecodeParams build(
        const H264Sps& sps,
        const H264Pps& pps,
        const H264SliceHeader* slice,
        uint32_t curr_surface,
        uint32_t frame_num,
        int32_t poc,
        const DecodedPictureBuffer& dpb,
        size_t slice_data_size
    );
};

/// Sovereign VA-API HEVC Parameter Packet for GPU Decode submission
struct VaapiHevcDecodeParams {
    VAPictureParameterBufferHEVC pic_param{};
    VASliceParameterBufferHEVC slice_param{};
    bool has_slice{false};

    static VaapiHevcDecodeParams build(
        const HevcSps& sps,
        const HevcPps& pps,
        const HevcSliceHeader* slice,
        uint32_t curr_surface,
        int32_t poc,
        const DecodedPictureBuffer& dpb,
        size_t slice_data_size
    );
};

/// Sovereign VA-API VP9 Parameter Packet for GPU Decode submission
struct VaapiVp9DecodeParams {
    VADecPictureParameterBufferVP9 pic_param{};

    static VaapiVp9DecodeParams build(
        const Vp9UncompressedHeader& hdr,
        uint32_t curr_surface
    );
};

/// Sovereign VA-API AV1 Parameter Packet for GPU Decode submission
struct VaapiAv1DecodeParams {
    VADecPictureParameterBufferAV1 pic_param{};

    static VaapiAv1DecodeParams build(
        const Av1SequenceHeader& seq,
        const Av1FrameHeader* frame,
        uint32_t curr_surface
    );
};

#endif

} // namespace atum
