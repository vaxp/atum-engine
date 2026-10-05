#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <span>
#include <array>
#include "atum/bitstream/bit_writer.hpp"
#include "atum/codec/video/h264/h264_sps.hpp"
#include "atum/codec/video/h264/h264_pps.hpp"
#include "atum/codec/video/h264/h264_slice.hpp"
#include "atum/codec/video/h264/h264_rdo.hpp"

namespace atum {

/// H.264 Context Adaptive Variable Length Coding (CAVLC) Residual Encoder.
class H264CavlcEncoder {
public:
    explicit H264CavlcEncoder(BitWriter& writer) : bw_(writer) {}

    /// Encodes a 4x4 transform residual block using CAVLC.
    /// `nC`: Left/Top context predictor for coeff_token table selection.
    /// Returns non-zero coefficient count (for updating neighbor nC).
    int32_t encode_residual4x4(const int32_t* block, int32_t nC);

    /// Encodes 4x4 Luma DC block (after Hadamard transform).
    int32_t encode_luma_dc4x4(const int32_t* block);

    /// Encodes 2x2 Chroma DC block (after Hadamard transform).
    int32_t encode_chroma_dc2x2(const int32_t* block);

    /// Encodes macroblock header and syntax in CAVLC mode.
    void encode_macroblock(
        const MacroblockDecision& mb,
        bool is_p_slice,
        int32_t mb_qp_delta,
        int32_t nC_left,
        int32_t nC_top
    );

private:
    BitWriter& bw_;
};

/// H.264 Context Adaptive Binary Arithmetic Coding (CABAC) Arithmetic State Machine.
class H264CabacEncoder {
public:
    explicit H264CabacEncoder(BitWriter& writer);

    /// Initializes all 460+ context models for given slice type and slice QP.
    void init_contexts(H264SliceType slice_type, int32_t slice_qp);

    /// Encodes a regular binary symbol using context `ctx_idx`.
    void encode_bin(uint32_t bin, uint32_t ctx_idx);

    /// Encodes an equiprobable bypass symbol (p = 0.5).
    void encode_bin_bypass(uint32_t bin);

    /// Encodes a terminating bin (for end of slice flag).
    void encode_bin_terminate(uint32_t bin);

    /// Flushes arithmetic state and pads remaining bits.
    void flush();

    /// Encodes a macroblock syntax structure via CABAC.
    void encode_macroblock(
        const MacroblockDecision& mb,
        bool is_p_slice,
        int32_t mb_qp_delta
    );

private:
    void renorm();
    void put_bit(uint32_t bit);

    BitWriter& bw_;
    uint32_t low_{0};
    uint32_t range_{510};
    uint32_t outstanding_bits_{0};

    // Context states: (pStateIdx << 1) | valMPS
    std::array<uint8_t, 460> contexts_{};
};

/// Annex-B NAL unit generator and parameter set serializer.
class H264NalSerializer {
public:
    /// Serializes Sequence Parameter Set (SPS) to RBSP.
    static std::vector<uint8_t> serialize_sps(const H264Sps& sps);

    /// Serializes Picture Parameter Set (PPS) to RBSP.
    static std::vector<uint8_t> serialize_pps(const H264Pps& pps);

    /// Serializes Slice Header to RBSP.
    static void serialize_slice_header(
        BitWriter& bw,
        const H264SliceHeader& header,
        const H264Sps& sps,
        const H264Pps& pps
    );

    /// Escapes RBSP to EBSP by inserting 0x03 emulation prevention bytes.
    static std::vector<uint8_t> escape_rbsp(std::span<const uint8_t> rbsp);

    /// Packages an RBSP payload into an Annex-B NAL unit with 4-byte start code.
    static std::vector<uint8_t> package_annexb(
        uint8_t nal_ref_idc,
        H264NalType nal_type,
        std::span<const uint8_t> rbsp
    );
};

} // namespace atum
