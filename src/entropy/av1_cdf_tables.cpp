#include "atum/entropy/av1_cdf_tables.hpp"
#include <cstring>

namespace atum {

namespace {

// Helper to initialize normalized CDF with equal steps
template <size_t N>
void init_uniform_cdf(uint16_t (&cdf)[N]) {
    size_t n_symbols = N - 1;
    uint32_t step = 32768 / n_symbols;
    for (size_t i = 0; i < n_symbols; ++i) {
        cdf[i] = static_cast<uint16_t>((i + 1) * step);
    }
    cdf[n_symbols - 1] = 32768;
    cdf[n_symbols] = 0; // count
}

} // namespace

Av1CdfContext Av1CdfTables::get_default_context() noexcept {
    Av1CdfContext ctx{};
    reset_to_defaults(ctx);
    return ctx;
}

void Av1CdfTables::reset_to_defaults(Av1CdfContext& ctx) noexcept {
    // 1. Partition CDFs (10 symbols)
    for (size_t c = 0; c < 10; ++c) {
        init_uniform_cdf(ctx.partition_cdf[c]);
        // Weight PARTITION_NONE, HORZ, VERT, SPLIT higher for natural imagery
        ctx.partition_cdf[c][0] = 10000;
        ctx.partition_cdf[c][1] = 16000;
        ctx.partition_cdf[c][2] = 22000;
        ctx.partition_cdf[c][3] = 26000;
        ctx.partition_cdf[c][4] = 27500;
        ctx.partition_cdf[c][5] = 29000;
        ctx.partition_cdf[c][6] = 30500;
        ctx.partition_cdf[c][7] = 31500;
        ctx.partition_cdf[c][8] = 32200;
        ctx.partition_cdf[c][9] = 32768;
        ctx.partition_cdf[c][10] = 0;
    }

    // 2. Y Mode CDFs (13 symbols)
    for (size_t c = 0; c < 4; ++c) {
        init_uniform_cdf(ctx.y_mode_cdf[c]);
        ctx.y_mode_cdf[c][0] = 8000;  // DC
        ctx.y_mode_cdf[c][1] = 14000; // V
        ctx.y_mode_cdf[c][2] = 20000; // H
    }

    // 3. UV Mode CDFs (14 symbols)
    for (size_t c = 0; c < 14; ++c) {
        init_uniform_cdf(ctx.uv_mode_cdf[c]);
    }

    // 4. Inter Mode CDFs (4 symbols: NEAREST, NEAR, GLOBAL, NEW)
    for (size_t c = 0; c < 8; ++c) {
        ctx.inter_mode_cdf[c][0] = 16384; // NEARESTMV
        ctx.inter_mode_cdf[c][1] = 24576; // NEARMV
        ctx.inter_mode_cdf[c][2] = 28672; // GLOBALMV
        ctx.inter_mode_cdf[c][3] = 32768; // NEWMV
        ctx.inter_mode_cdf[c][4] = 0;
    }

    // 5. Compound Inter Mode CDFs (8 symbols)
    for (size_t c = 0; c < 8; ++c) {
        init_uniform_cdf(ctx.comp_inter_mode_cdf[c]);
    }

    // 6. Restoration Type CDF (3 symbols: None, Wiener, Sgrproj)
    for (size_t c = 0; c < 4; ++c) {
        ctx.restore_cdf[c][0] = 10922; // None
        ctx.restore_cdf[c][1] = 21844; // Wiener
        ctx.restore_cdf[c][2] = 32768; // Sgrproj
        ctx.restore_cdf[c][3] = 0;
    }

    // 7. Skip Block CDF (2 symbols)
    for (size_t c = 0; c < 3; ++c) {
        ctx.skip_cdf[c][0] = 16384;
        ctx.skip_cdf[c][1] = 32768;
        ctx.skip_cdf[c][2] = 0;
    }

    // 8. Transform Size CDF (4 symbols)
    for (size_t c = 0; c < 4; ++c) {
        ctx.tx_size_cdf[c][0] = 8192;
        ctx.tx_size_cdf[c][1] = 16384;
        ctx.tx_size_cdf[c][2] = 24576;
        ctx.tx_size_cdf[c][3] = 32768;
        ctx.tx_size_cdf[c][4] = 0;
    }
}

} // namespace atum
