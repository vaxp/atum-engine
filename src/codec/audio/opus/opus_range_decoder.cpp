#include "atum/codec/audio/opus/opus_range_decoder.hpp"
#include <algorithm>

namespace atum {

static constexpr uint32_t EC_CODE_TOP = 0x80000000U;
static constexpr uint32_t EC_CODE_BOT = (EC_CODE_TOP >> 8); // 0x00800000U

void OpusRangeDecoder::init(std::span<const uint8_t> data) noexcept {
    buf_ = data;
    offs_ = 0;
    rem_ = 0;
    error_ = false;
    bits_consumed_ = 0;

    if (buf_.empty()) {
        error_ = true;
        rng_ = EC_CODE_BOT;
        val_ = 0;
        return;
    }

    rem_ = read_byte();
    rng_ = 1u << 7;
    val_ = rng_ - 1 - (static_cast<uint32_t>(rem_) >> 1);
    ext_ = 0;
    normalize();
}

uint8_t OpusRangeDecoder::read_byte() noexcept {
    if (offs_ < buf_.size()) {
        return buf_[offs_++];
    }
    return 0;
}

void OpusRangeDecoder::normalize() noexcept {
    while (rng_ <= EC_CODE_BOT) {
        bits_consumed_ += 8;
        rng_ <<= 8;
        int sym = rem_;
        rem_ = read_byte();
        sym = ((sym << 1) | (rem_ >> 7)) & 0xFF;
        val_ = ((val_ << 8) + (255 - sym)) & 0x7FFFFFFF;
    }
}

uint32_t OpusRangeDecoder::decode(uint32_t ft) noexcept {
    if (ft == 0) return 0;
    ext_ = rng_ / ft;
    uint32_t s = val_ / ext_;
    return ft - std::min(s + 1, ft);
}

void OpusRangeDecoder::update(uint32_t fl, uint32_t fh, uint32_t ft) noexcept {
    uint32_t s = ext_ * (ft - fh);
    val_ -= s;
    rng_ = (fl > 0) ? (ext_ * (fh - fl)) : (rng_ - s);
    normalize();
}

uint32_t OpusRangeDecoder::decode_bits(uint32_t nbits) noexcept {
    if (nbits == 0) return 0;
    if (nbits <= 8) {
        uint32_t ft = 1u << nbits;
        uint32_t s = decode(ft);
        update(s, s + 1, ft);
        return s;
    }
    uint32_t high = decode_bits(8);
    uint32_t low = decode_bits(nbits - 8);
    return (high << (nbits - 8)) | low;
}

uint32_t OpusRangeDecoder::decode_uint(uint32_t total) noexcept {
    if (total <= 1) return 0;
    if (total <= 65536) {
        uint32_t s = decode(total);
        update(s, s + 1, total);
        return s;
    }
    uint32_t ft = (total + 255) / 256;
    uint32_t s = decode_uint(ft);
    uint32_t rem = decode_bits(8);
    uint32_t res = (s << 8) | rem;
    return (res < total) ? res : (total - 1);
}

uint32_t OpusRangeDecoder::tell_bits() const noexcept {
    return bits_consumed_;
}

} // namespace atum
