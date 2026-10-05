#include "atum/codec/audio/flac/flac_decoder.hpp"
#include <cstring>

namespace atum {

static int32_t sign_extend(uint32_t val, uint32_t bits) noexcept {
    if (bits == 0 || bits >= 32) return static_cast<int32_t>(val);
    uint32_t sign_bit = 1u << (bits - 1);
    return static_cast<int32_t>((val ^ sign_bit) - sign_bit);
}

FlacDecoder::FlacDecoder() {
    params_.codec_id = CodecId::FLAC;
    params_.sample_rate = 44100;
    params_.channels = 2;
    params_.channel_layout = ChannelLayout::Stereo;
    params_.audio_format = AudioSampleFormat::S16LE;
}

MediaResult<void> FlacDecoder::init(const CodecParameters& params) {
    params_ = params;
    if (params.sample_rate > 0) sample_rate_ = params.sample_rate;
    if (params.channels > 0) channels_ = static_cast<uint8_t>(params.channels);
    layout_ = (channels_ == 1) ? ChannelLayout::Mono : ChannelLayout::Stereo;

    if (!params.extradata.empty()) {
        auto sinfo_res = FlacParser::parse_streaminfo(params.extradata);
        if (sinfo_res) {
            streaminfo_ = *sinfo_res;
            sample_rate_ = streaminfo_.sample_rate;
            channels_ = streaminfo_.channels;
            bits_per_sample_ = streaminfo_.bits_per_sample;
            layout_ = (channels_ == 1) ? ChannelLayout::Mono : ChannelLayout::Stereo;
        }
    }

    while (!frame_queue_.empty()) frame_queue_.pop();
    return {};
}

MediaResult<void> FlacDecoder::flush() {
    while (!frame_queue_.empty()) frame_queue_.pop();
    return {};
}

MediaResult<void> FlacDecoder::reset() {
    return flush();
}

MediaResult<void> FlacDecoder::send_packet(const Packet& packet) {
    if (packet.empty()) return MediaError::NeedMoreData;

    std::span<const uint8_t> data(packet.data(), packet.size());
    size_t offset = 0;

    auto sync_opt = FlacParser::find_sync(data);
    if (!sync_opt) return MediaError::InvalidData;
    offset = *sync_opt;

    auto hdr_res = FlacParser::parse_frame_header(data.subspan(offset));
    if (!hdr_res) return hdr_res.error();
    const auto& hdr = *hdr_res;

    sample_rate_ = hdr.sample_rate;
    channels_ = hdr.channels;
    bits_per_sample_ = hdr.bits_per_sample;
    layout_ = (channels_ == 1) ? ChannelLayout::Mono : ChannelLayout::Stereo;

    BitReader reader(data.subspan(offset + hdr.header_bytes));

    std::vector<std::vector<int32_t>> channel_samples(channels_);
    for (uint8_t ch = 0; ch < channels_; ++ch) {
        uint8_t subframe_bps = bits_per_sample_;
        // In LeftSide / RightSide / MidSide, side channel has +1 bit
        if (hdr.channel_mode == FlacChannelMode::LeftSide && ch == 1) {
            subframe_bps += 1;
        } else if (hdr.channel_mode == FlacChannelMode::RightSide && ch == 1) {
            subframe_bps += 1;
        } else if (hdr.channel_mode == FlacChannelMode::MidSide && ch == 1) {
            subframe_bps += 1;
        }

        auto sf_res = decode_subframe(reader, subframe_bps, hdr.block_size, channel_samples[ch]);
        if (!sf_res) return sf_res.error();
    }

    // Byte alignment padding at end of subframes
    reader.align_to_byte();

    // Channel decorrelation
    if (hdr.channel_mode == FlacChannelMode::LeftSide && channels_ == 2) {
        for (size_t i = 0; i < hdr.block_size; ++i) {
            channel_samples[1][i] = channel_samples[0][i] - channel_samples[1][i];
        }
    } else if (hdr.channel_mode == FlacChannelMode::RightSide && channels_ == 2) {
        for (size_t i = 0; i < hdr.block_size; ++i) {
            channel_samples[0][i] = channel_samples[1][i] + channel_samples[0][i];
        }
    } else if (hdr.channel_mode == FlacChannelMode::MidSide && channels_ == 2) {
        for (size_t i = 0; i < hdr.block_size; ++i) {
            int32_t mid = channel_samples[0][i];
            int32_t side = channel_samples[1][i];
            mid = (mid << 1) | (side & 1);
            channel_samples[0][i] = (mid + side) >> 1;
            channel_samples[1][i] = (mid - side) >> 1;
        }
    }

    // Allocate AudioFrame
    AudioSampleFormat out_fmt = (bits_per_sample_ <= 16) ? AudioSampleFormat::S16LE : AudioSampleFormat::S32LE;
    AudioFrame out_frame = AudioFrame::allocate(
        sample_rate_,
        channels_,
        out_fmt,
        hdr.block_size,
        layout_
    );
    out_frame.set_pts(packet.pts());
    out_frame.set_dts(packet.dts());

    uint8_t* out_ptr = out_frame.data_mut();
    if (out_fmt == AudioSampleFormat::S16LE) {
        int16_t* s16 = reinterpret_cast<int16_t*>(out_ptr);
        for (size_t i = 0; i < hdr.block_size; ++i) {
            for (uint8_t ch = 0; ch < channels_; ++ch) {
                *s16++ = static_cast<int16_t>(channel_samples[ch][i]);
            }
        }
    } else {
        int32_t* s32 = reinterpret_cast<int32_t*>(out_ptr);
        for (size_t i = 0; i < hdr.block_size; ++i) {
            for (uint8_t ch = 0; ch < channels_; ++ch) {
                *s32++ = channel_samples[ch][i];
            }
        }
    }

    frame_queue_.push(std::move(out_frame));
    return {};
}

MediaResult<void> FlacDecoder::decode_subframe(
    BitReader& reader,
    uint8_t bps,
    uint16_t block_size,
    std::vector<int32_t>& samples
) {
    samples.resize(block_size, 0);

    auto pad_res = reader.read_bit();
    if (!pad_res) return pad_res.error(); // zero padding bit
    if (*pad_res != 0) return MediaError::InvalidData;

    auto type_res = reader.read_bits(6);
    if (!type_res) return type_res.error();
    uint8_t type = static_cast<uint8_t>(*type_res);

    auto wasted_flag = reader.read_bit();
    if (!wasted_flag) return wasted_flag.error();
    uint8_t wasted_bits = 0;
    if (*wasted_flag != 0) {
        while (true) {
            auto b = reader.read_bit();
            if (!b) return b.error();
            wasted_bits++;
            if (*b != 0) break;
        }
        bps -= wasted_bits;
    }

    if (type == 0) {
        // SUBFRAME_CONSTANT
        auto val_res = reader.read_bits(bps);
        if (!val_res) return val_res.error();
        int32_t cval = sign_extend(*val_res, bps);
        std::fill(samples.begin(), samples.end(), cval);
    } else if (type == 1) {
        // SUBFRAME_VERBATIM
        for (size_t i = 0; i < block_size; ++i) {
            auto s_res = reader.read_bits(bps);
            if (!s_res) return s_res.error();
            samples[i] = sign_extend(*s_res, bps);
        }
    } else if (type >= 8 && type <= 12) {
        // SUBFRAME_FIXED (order 0..4)
        uint8_t order = type - 8;
        for (uint8_t i = 0; i < order; ++i) {
            auto w_res = reader.read_bits(bps);
            if (!w_res) return w_res.error();
            samples[i] = sign_extend(*w_res, bps);
        }

        std::vector<int32_t> residual;
        auto res_ret = decode_residual(reader, order, block_size, residual);
        if (!res_ret) return res_ret;

        for (size_t i = order; i < block_size; ++i) {
            int32_t pred = 0;
            switch (order) {
                case 0: pred = 0; break;
                case 1: pred = samples[i - 1]; break;
                case 2: pred = 2 * samples[i - 1] - samples[i - 2]; break;
                case 3: pred = 3 * samples[i - 1] - 3 * samples[i - 2] + samples[i - 3]; break;
                case 4: pred = 4 * samples[i - 1] - 6 * samples[i - 2] + 4 * samples[i - 3] - samples[i - 4]; break;
            }
            samples[i] = pred + residual[i - order];
        }
    } else if (type >= 32) {
        // SUBFRAME_LPC (order 1..32)
        uint8_t order = (type - 32) + 1;
        for (uint8_t i = 0; i < order; ++i) {
            auto w_res = reader.read_bits(bps);
            if (!w_res) return w_res.error();
            samples[i] = sign_extend(*w_res, bps);
        }

        auto prec_res = reader.read_bits(4);
        if (!prec_res) return prec_res.error();
        uint8_t c_prec = static_cast<uint8_t>(*prec_res) + 1;

        auto shift_res = reader.read_bits(5);
        if (!shift_res) return shift_res.error();
        int32_t c_shift = sign_extend(*shift_res, 5);

        std::vector<int32_t> coeffs(order);
        for (uint8_t i = 0; i < order; ++i) {
            auto c_res = reader.read_bits(c_prec);
            if (!c_res) return c_res.error();
            coeffs[i] = sign_extend(*c_res, c_prec);
        }

        std::vector<int32_t> residual;
        auto res_ret = decode_residual(reader, order, block_size, residual);
        if (!res_ret) return res_ret;

        for (size_t i = order; i < block_size; ++i) {
            int64_t sum = 0;
            for (uint8_t j = 0; j < order; ++j) {
                sum += static_cast<int64_t>(coeffs[j]) * samples[i - 1 - j];
            }
            int32_t pred = static_cast<int32_t>(sum >> c_shift);
            samples[i] = pred + residual[i - order];
        }
    } else {
        return MediaError::InvalidData;
    }

    if (wasted_bits > 0) {
        for (size_t i = 0; i < block_size; ++i) {
            samples[i] <<= wasted_bits;
        }
    }

    return {};
}

MediaResult<void> FlacDecoder::decode_residual(
    BitReader& reader,
    uint8_t predictor_order,
    uint16_t block_size,
    std::vector<int32_t>& residual
) {
    auto method_res = reader.read_bits(2);
    if (!method_res) return method_res.error();
    uint8_t method = static_cast<uint8_t>(*method_res);
    uint32_t param_bits = (method == 0) ? 4 : 5;
    uint32_t escape_param = (1u << param_bits) - 1;

    auto order_res = reader.read_bits(4);
    if (!order_res) return order_res.error();
    uint8_t partition_order = static_cast<uint8_t>(*order_res);
    uint32_t num_partitions = 1u << partition_order;

    residual.reserve(block_size - predictor_order);

    for (uint32_t p = 0; p < num_partitions; ++p) {
        auto k_res = reader.read_bits(param_bits);
        if (!k_res) return k_res.error();
        uint32_t k = *k_res;

        uint32_t part_len = block_size / num_partitions;
        uint32_t sample_count = (partition_order == 0)
            ? (block_size - predictor_order)
            : ((p == 0) ? (part_len - predictor_order) : part_len);

        if (k == escape_param) {
            // Unencoded escape partition
            auto esc_bps_res = reader.read_bits(5);
            if (!esc_bps_res) return esc_bps_res.error();
            uint32_t esc_bps = *esc_bps_res;

            for (uint32_t i = 0; i < sample_count; ++i) {
                auto r_res = reader.read_bits(esc_bps);
                if (!r_res) return r_res.error();
                residual.push_back(sign_extend(*r_res, esc_bps));
            }
        } else {
            // Rice-coded partition
            for (uint32_t i = 0; i < sample_count; ++i) {
                // Unary quotient
                uint32_t q = 0;
                while (true) {
                    auto b = reader.read_bit();
                    if (!b) return b.error();
                    if (*b != 0) break;
                    q++;
                }

                uint32_t rem = 0;
                if (k > 0) {
                    auto rem_res = reader.read_bits(k);
                    if (!rem_res) return rem_res.error();
                    rem = *rem_res;
                }

                uint32_t u = (q << k) | rem;
                int32_t val = (u & 1) ? -static_cast<int32_t>((u + 1) >> 1) : static_cast<int32_t>(u >> 1);
                residual.push_back(val);
            }
        }
    }

    return {};
}

MediaResult<void> FlacDecoder::receive_frame(AudioFrame& frame) {
    if (frame_queue_.empty()) {
        return MediaError::NeedMoreData;
    }
    frame = std::move(frame_queue_.front());
    frame_queue_.pop();
    return {};
}

} // namespace atum
