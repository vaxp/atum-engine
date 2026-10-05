#include "atum/entropy/vp9_probability_tables.hpp"
#include <cstring>

namespace atum {

namespace {

// Canonical VP9 default partition probabilities (Appendix B, Table B.1)
const uint8_t DEFAULT_PARTITION_PROBS[4][4][3] = {
    // 8x8 block level
    { { 199, 122, 141 }, { 174, 114, 139 }, { 156, 108, 138 }, { 168, 111, 138 } },
    // 16x16 block level
    { { 120, 115, 120 }, { 105, 107, 116 }, {  89,  99, 112 }, { 100, 103, 115 } },
    // 32x32 block level
    { {  84,  98, 115 }, {  71,  92, 111 }, {  58,  83, 106 }, {  66,  89, 110 } },
    // 64x64 block level
    { {  43,  79, 108 }, {  34,  72, 104 }, {  26,  62,  98 }, {  30,  69, 102 } }
};

// Canonical VP9 default 10-mode intra prediction probabilities (Table B.2)
const uint8_t DEFAULT_INTRA_MODE_PROBS[4][9] = {
    {  65,  32,   1, 144,  59,  12,   1, 162, 111 },
    {  56,  36,   1, 134,  64,  13,   1, 150, 129 },
    {  53,  38,   1, 130,  67,  13,   1, 144, 137 },
    {  57,  41,   1, 133,  64,  13,   1, 148, 133 }
};

// Canonical VP9 default UV mode probabilities given Luma mode (Table B.3)
const uint8_t DEFAULT_UV_MODE_PROBS[10][9] = {
    { 120, 100, 100, 100, 100, 100, 100, 100, 100 }, // DC
    { 100, 140,  80,  80,  80,  80,  80,  80,  80 }, // V
    { 100,  80, 140,  80,  80,  80,  80,  80,  80 }, // H
    {  80,  80,  80, 140,  80,  80,  80,  80,  80 }, // D45
    {  80,  80,  80,  80, 140,  80,  80,  80,  80 }, // D135
    {  80,  80,  80,  80,  80, 140,  80,  80,  80 }, // D117
    {  80,  80,  80,  80,  80,  80, 140,  80,  80 }, // D153
    {  80,  80,  80,  80,  80,  80,  80, 140,  80 }, // D207
    {  80,  80,  80,  80,  80,  80,  80,  80, 140 }, // D63
    { 110,  90,  90,  90,  90,  90,  90,  90,  90 }  // TM
};

// Canonical VP9 default 4-mode inter prediction probabilities (Table B.4)
const uint8_t DEFAULT_INTER_MODE_PROBS[7][3] = {
    {   2, 173,  34 },
    {   7, 145,  85 },
    {  24, 101,  44 },
    {  43, 176, 240 },
    {  36, 115, 149 },
    {   1, 255,   0 },
    {   1, 110,  93 }
};

// Canonical VP9 default switchable interpolation filter probabilities (Table B.5)
const uint8_t DEFAULT_SWITCHABLE_INTERP_PROBS[4][2] = {
    { 235, 162 },
    {  36, 255 },
    { 212, 147 },
    {   4, 255 }
};

// Canonical VP9 default Intra vs Inter probabilities (Table B.6)
const uint8_t DEFAULT_INTRA_INTER_PROB[4] = { 9, 102, 187, 225 };

// Canonical VP9 default Compound prediction probabilities (Table B.7)
const uint8_t DEFAULT_COMP_INTER_PROB[5] = { 8, 32, 112, 171, 218 };
const uint8_t DEFAULT_COMP_REF_PROB[5]   = { 50, 126, 123, 221, 226 };
const uint8_t DEFAULT_SINGLE_REF_PROB[5][2] = {
    { 33,  16 },
    { 77,  74 },
    { 141, 96 },
    { 204, 77 },
    { 189, 27 }
};

const uint8_t DEFAULT_SKIP_PROBS[3] = { 192, 128, 64 };

} // namespace

Vp9FrameContext Vp9ProbabilityTables::get_default_context() noexcept {
    Vp9FrameContext ctx{};
    reset_to_defaults(ctx);
    return ctx;
}

void Vp9ProbabilityTables::reset_to_defaults(Vp9FrameContext& ctx) noexcept {
    std::memcpy(ctx.partition_probs, DEFAULT_PARTITION_PROBS, sizeof(ctx.partition_probs));
    std::memcpy(ctx.intra_mode_probs, DEFAULT_INTRA_MODE_PROBS, sizeof(ctx.intra_mode_probs));
    std::memcpy(ctx.uv_mode_probs, DEFAULT_UV_MODE_PROBS, sizeof(ctx.uv_mode_probs));
    std::memcpy(ctx.inter_mode_probs, DEFAULT_INTER_MODE_PROBS, sizeof(ctx.inter_mode_probs));
    std::memcpy(ctx.switchable_interp_probs, DEFAULT_SWITCHABLE_INTERP_PROBS, sizeof(ctx.switchable_interp_probs));
    std::memcpy(ctx.intra_inter_prob, DEFAULT_INTRA_INTER_PROB, sizeof(ctx.intra_inter_prob));
    std::memcpy(ctx.comp_inter_prob, DEFAULT_COMP_INTER_PROB, sizeof(ctx.comp_inter_prob));
    std::memcpy(ctx.comp_ref_prob, DEFAULT_COMP_REF_PROB, sizeof(ctx.comp_ref_prob));
    std::memcpy(ctx.single_ref_prob, DEFAULT_SINGLE_REF_PROB, sizeof(ctx.single_ref_prob));
    std::memcpy(ctx.skip_probs, DEFAULT_SKIP_PROBS, sizeof(ctx.skip_probs));

    ctx.tx_probs_8x8[0][0] = 100;
    ctx.tx_probs_8x8[1][0] = 66;

    ctx.tx_probs_16x16[0][0] = 20;
    ctx.tx_probs_16x16[0][1] = 140;
    ctx.tx_probs_16x16[1][0] = 15;
    ctx.tx_probs_16x16[1][1] = 76;

    ctx.tx_probs_32x32[0][0] = 11;
    ctx.tx_probs_32x32[0][1] = 65;
    ctx.tx_probs_32x32[0][2] = 177;
    ctx.tx_probs_32x32[1][0] = 8;
    ctx.tx_probs_32x32[1][1] = 34;
    ctx.tx_probs_32x32[1][2] = 107;

    ctx.mv_joint_probs[0] = 32;
    ctx.mv_joint_probs[1] = 64;
    ctx.mv_joint_probs[2] = 96;

    ctx.mv_sign_prob[0] = 128;
    ctx.mv_sign_prob[1] = 128;
    ctx.mv_class0_bit_prob[0] = 216;
    ctx.mv_class0_bit_prob[1] = 216;
}

} // namespace atum
