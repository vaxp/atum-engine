#include "atum/codec/audio/opus/opus_parser.hpp"

namespace atum {

MediaResult<OpusToc> OpusParser::parse_toc(uint8_t toc_byte) noexcept {
    OpusToc toc{};
    toc.config = (toc_byte >> 3) & 0x1F;
    toc.stereo = ((toc_byte >> 2) & 1) != 0;
    toc.code = toc_byte & 0x03;

    // Determine Mode, Bandwidth, and Duration (RFC 6716 Table 2)
    if (toc.config <= 11) {
        toc.mode = OpusMode::SilkOnly;
        if (toc.config <= 3) {
            toc.bandwidth = OpusBandwidth::Narrowband;
        } else if (toc.config <= 7) {
            toc.bandwidth = OpusBandwidth::Mediumband;
        } else {
            toc.bandwidth = OpusBandwidth::Wideband;
        }

        uint8_t dur_idx = toc.config % 4;
        static constexpr float DUR_MS[4] = {10.0f, 20.0f, 40.0f, 60.0f};
        static constexpr size_t SAMPLES_48K[4] = {480, 960, 1920, 2880};
        toc.frame_duration_ms = DUR_MS[dur_idx];
        toc.frame_samples = SAMPLES_48K[dur_idx];
    } else if (toc.config <= 15) {
        toc.mode = OpusMode::Hybrid;
        toc.bandwidth = (toc.config <= 13) ? OpusBandwidth::Superwideband : OpusBandwidth::Fullband;
        uint8_t dur_idx = (toc.config - 12) % 2;
        static constexpr float DUR_MS[2] = {10.0f, 20.0f};
        static constexpr size_t SAMPLES_48K[2] = {480, 960};
        toc.frame_duration_ms = DUR_MS[dur_idx];
        toc.frame_samples = SAMPLES_48K[dur_idx];
    } else {
        toc.mode = OpusMode::CeltOnly;
        uint8_t bw_idx = (toc.config - 16) / 4;
        static constexpr OpusBandwidth BWS[4] = {
            OpusBandwidth::Narrowband,
            OpusBandwidth::Wideband,
            OpusBandwidth::Superwideband,
            OpusBandwidth::Fullband
        };
        toc.bandwidth = BWS[bw_idx];

        uint8_t dur_idx = (toc.config - 16) % 4;
        static constexpr float DUR_MS[4] = {2.5f, 5.0f, 10.0f, 20.0f};
        static constexpr size_t SAMPLES_48K[4] = {120, 240, 480, 960};
        toc.frame_duration_ms = DUR_MS[dur_idx];
        toc.frame_samples = SAMPLES_48K[dur_idx];
    }

    return toc;
}

MediaResult<OpusPacket> OpusParser::parse_packet(std::span<const uint8_t> data) noexcept {
    if (data.empty()) return MediaError::NeedMoreData;

    auto toc_res = parse_toc(data[0]);
    if (!toc_res) return toc_res.error();

    OpusPacket pkt{};
    pkt.toc = *toc_res;
    std::span<const uint8_t> body = data.subspan(1);

    switch (pkt.toc.code) {
        case 0: { // 1 frame
            pkt.frames.push_back(OpusFrame{body, pkt.toc.frame_samples});
            pkt.total_samples = pkt.toc.frame_samples;
            break;
        }

        case 1: { // 2 frames of equal size
            if (body.size() % 2 != 0) return MediaError::InvalidData;
            size_t frame_sz = body.size() / 2;
            pkt.frames.push_back(OpusFrame{body.subspan(0, frame_sz), pkt.toc.frame_samples});
            pkt.frames.push_back(OpusFrame{body.subspan(frame_sz, frame_sz), pkt.toc.frame_samples});
            pkt.total_samples = pkt.toc.frame_samples * 2;
            break;
        }

        case 2: { // 2 frames of different sizes
            if (body.empty()) return MediaError::NeedMoreData;
            size_t pos = 0;
            size_t len1 = body[pos++];
            if (len1 >= 252) {
                if (pos >= body.size()) return MediaError::NeedMoreData;
                len1 = len1 + 4 * body[pos++];
            }
            if (pos + len1 > body.size()) return MediaError::InvalidData;
            size_t len2 = body.size() - pos - len1;

            pkt.frames.push_back(OpusFrame{body.subspan(pos, len1), pkt.toc.frame_samples});
            pkt.frames.push_back(OpusFrame{body.subspan(pos + len1, len2), pkt.toc.frame_samples});
            pkt.total_samples = pkt.toc.frame_samples * 2;
            break;
        }

        case 3: { // Arbitrary number of frames (RFC 6716 Section 3.2.5)
            if (body.empty()) return MediaError::NeedMoreData;
            size_t pos = 0;
            uint8_t frame_count_byte = body[pos++];
            bool vbr = (frame_count_byte & 0x80) != 0;
            bool has_padding = (frame_count_byte & 0x40) != 0;
            uint8_t count = frame_count_byte & 0x3F;
            if (count == 0 || count > 48) return MediaError::InvalidData;

            size_t padding_len = 0;
            if (has_padding) {
                while (pos < body.size()) {
                    uint8_t p = body[pos++];
                    if (p == 255) {
                        padding_len += 254;
                    } else {
                        padding_len += p;
                        break;
                    }
                }
            }

            if (pos + padding_len > body.size()) return MediaError::InvalidData;
            size_t payload_len = body.size() - pos - padding_len;

            if (!vbr) {
                // CBR: equal sized frames
                if (payload_len % count != 0) return MediaError::InvalidData;
                size_t frame_sz = payload_len / count;
                for (uint8_t i = 0; i < count; ++i) {
                    pkt.frames.push_back(OpusFrame{body.subspan(pos + i * frame_sz, frame_sz), pkt.toc.frame_samples});
                }
            } else {
                // VBR: variable sized frames
                std::vector<size_t> frame_lens;
                size_t total_vbr_lens = 0;
                for (uint8_t i = 0; i + 1 < count; ++i) {
                    if (pos >= body.size() - padding_len) return MediaError::NeedMoreData;
                    size_t len = body[pos++];
                    if (len >= 252) {
                        if (pos >= body.size() - padding_len) return MediaError::NeedMoreData;
                        len = len + 4 * body[pos++];
                    }
                    frame_lens.push_back(len);
                    total_vbr_lens += len;
                }
                if (total_vbr_lens > payload_len) return MediaError::InvalidData;
                frame_lens.push_back(payload_len - total_vbr_lens);

                for (size_t len : frame_lens) {
                    pkt.frames.push_back(OpusFrame{body.subspan(pos, len), pkt.toc.frame_samples});
                    pos += len;
                }
            }
            pkt.total_samples = pkt.toc.frame_samples * count;
            break;
        }
    }

    return pkt;
}

} // namespace atum
