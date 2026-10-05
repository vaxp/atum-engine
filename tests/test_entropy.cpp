#include <cassert>
#include <iostream>
#include <vector>
#include <array>
#include "atum/entropy/cabac.hpp"
#include "atum/entropy/vp9_bool_decoder.hpp"
#include "atum/entropy/av1_symbol_decoder.hpp"

using namespace atum;

void test_cabac_context_init() {
    std::cout << "[Test] CABAC Context Model Initialization..." << std::endl;
    CabacContext ctx{};
    // QP = 26, m = 11, n = -45
    ctx.init(26, 11, -45);
    assert(ctx.state <= 63);
    assert(ctx.mps == 0 || ctx.mps == 1);

    // High QP saturation
    ctx.init(51, 20, 10);
    assert(ctx.state <= 63);
    std::cout << "  -> Passed!" << std::endl;
}

void test_cabac_decoding() {
    std::cout << "[Test] CABAC Bit Decoding & Bypass..." << std::endl;
    // Synthetic bitstream with diverse byte patterns
    std::vector<uint8_t> payload = {
        0xAA, 0x55, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC,
        0xDE, 0xF0, 0x00, 0xFF, 0x80, 0x40, 0x20, 0x10
    };

    CabacDecoder decoder(payload);
    assert(decoder.range() == 510);

    CabacContext ctx(10, 0);
    for (int i = 0; i < 20; ++i) {
        uint32_t bin = decoder.decode_bin(ctx);
        assert(bin == 0 || bin == 1);
        assert(ctx.state <= 63);
    }

    // Test bypass bins
    uint32_t bypass_val = decoder.decode_bypass_bins(8);
    assert(bypass_val < 256);

    // Test Exp-Golomb bypass decoding
    uint32_t eg0 = decoder.decode_exp_golomb(0);
    (void)eg0;

    // Test termination
    uint32_t term = decoder.decode_terminate();
    assert(term == 0 || term == 1);

    std::cout << "  -> Passed!" << std::endl;
}

void test_vp9_bool_decoder() {
    std::cout << "[Test] VP9 BoolDecoder & Tree Decoding..." << std::endl;
    std::vector<uint8_t> payload = {
        0x80, 0x40, 0x20, 0x10, 0xAA, 0x55, 0xFF, 0x00, 0x12, 0x34
    };

    Vp9BoolDecoder decoder(payload);

    // Test decoding with various probabilities
    for (int i = 0; i < 16; ++i) {
        uint32_t bit50 = decoder.decode_bool(128);
        assert(bit50 == 0 || bit50 == 1);

        uint32_t bit90 = decoder.decode_bool(230);
        assert(bit90 == 0 || bit90 == 1);
    }

    // Test literals
    uint32_t lit4 = decoder.decode_literal(4);
    assert(lit4 < 16);

    // Test binary decision tree
    // Tree for 4 symbols:
    // Node 0: -> 2 or 4. If 0 -> left (symbol 0), if 1 -> right (node 2)
    // Node 2: -> symbol 1 or 2
    static const int8_t sample_tree[] = {
        -0, 2,
        -1, -2
    };
    static const uint8_t sample_probs[] = { 128, 128 };

    int32_t sym = decoder.decode_tree(sample_tree, sample_probs);
    assert(sym >= 0 && sym <= 2);

    std::cout << "  -> Passed!" << std::endl;
}

void test_av1_symbol_decoder() {
    std::cout << "[Test] AV1 SymbolDecoder & CDF Adaptation..." << std::endl;
    std::vector<uint8_t> payload = {
        0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x01, 0x23,
        0x55, 0xAA, 0x33, 0xCC, 0x0F, 0xF0, 0x77, 0x88
    };

    Av1SymbolDecoder decoder(payload);
    assert(decoder.range() == 32768);

    // 4-symbol CDF: cumulative probabilities for symbols 0, 1, 2, 3
    // CDF table has size 4 + 1 for adaptation count
    uint16_t cdf4[5] = { 24576, 16384, 8192, 0, 0 };

    uint32_t sym0 = decoder.decode_symbol(cdf4, 4, true);
    assert(sym0 <= 3);
    // Ensure adaptation updated the count
    assert(cdf4[4] > 0);

    // Test equiprobable bit decoding
    uint32_t bit = decoder.decode_bit();
    assert(bit == 0 || bit == 1);

    uint32_t lit = decoder.decode_literal(6);
    assert(lit < 64);

    std::cout << "  -> Passed!" << std::endl;
}

int main() {
    std::cout << "=== Running Nisaba Media Arithmetic Entropy Tests ===" << std::endl;
    test_cabac_context_init();
    test_cabac_decoding();
    test_vp9_bool_decoder();
    test_av1_symbol_decoder();
    std::cout << "=== All Arithmetic Entropy Tests Passed Successfully! ===" << std::endl;
    return 0;
}
