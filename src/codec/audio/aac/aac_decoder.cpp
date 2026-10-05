#include "atum/codec/audio/aac/aac_decoder.hpp"
#include <cstring>
#include <vector>

namespace atum {

AacDecoder::AacDecoder() {
    params_.codec_id = CodecId::AAC;
    params_.sample_rate = 44100;
    params_.channels = 2;
    params_.channel_layout = ChannelLayout::Stereo;
    params_.audio_format = AudioSampleFormat::F32LE;
}

MediaResult<void> AacDecoder::init(const CodecParameters& params) {
    params_ = params;
    if (params.sample_rate > 0) sample_rate_ = params.sample_rate;
    if (params.channels > 0) channels_ = params.channels;
    layout_ = (channels_ == 1) ? ChannelLayout::Mono : ChannelLayout::Stereo;

    // Parse AudioSpecificConfig from extradata if present (ISO/IEC 14496-3 Section 1.6.2.1)
    if (!params.extradata.empty()) {
        BitReader reader(params.extradata);
        auto aot_res = reader.read_bits(5);
        if (aot_res) {
            auto sfi_res = reader.read_bits(4);
            if (sfi_res) {
                uint8_t sfi = static_cast<uint8_t>(*sfi_res);
                if (sfi == 15) {
                    auto custom_sr = reader.read_bits(24);
                    if (custom_sr) sample_rate_ = *custom_sr;
                } else {
                    sample_rate_ = aac_sample_rate_from_index(sfi);
                }
                auto chan_res = reader.read_bits(4);
                if (chan_res) {
                    channels_ = *chan_res;
                    layout_ = aac_channel_layout_from_config(static_cast<uint8_t>(channels_));
                }
            }
        }
    }

    imdct_engine_.reset();
    while (!frame_queue_.empty()) frame_queue_.pop();
    return {};
}

MediaResult<void> AacDecoder::flush() {
    imdct_engine_.reset();
    while (!frame_queue_.empty()) frame_queue_.pop();
    return {};
}

MediaResult<void> AacDecoder::reset() {
    return flush();
}

MediaResult<void> AacDecoder::send_packet(const Packet& packet) {
    if (packet.empty()) return MediaError::NeedMoreData;

    std::span<const uint8_t> payload(packet.data(), packet.size());
    size_t offset = 0;

    // Check if packet starts with ADTS syncword (0xFFF)
    if (payload.size() >= 7 && payload[0] == 0xFF && (payload[1] & 0xF0) == 0xF0) {
        auto adts_res = AacAdtsParser::parse(payload);
        if (adts_res) {
            const auto& hdr = *adts_res;
            sample_rate_ = hdr.sample_rate;
            channels_ = hdr.channel_configuration;
            layout_ = hdr.channel_layout;
            offset = hdr.header_size;
        }
    }

    BitReader reader(payload.subspan(offset));
    return decode_raw_block(reader, packet.pts(), packet.dts());
}

MediaResult<void> AacDecoder::decode_raw_block(BitReader& reader, int64_t pts, int64_t dts) {
    std::vector<float> pcm_out;

    while (reader.bits_left() >= 3) {
        auto id_res = reader.read_bits(3);
        if (!id_res) break;
        uint32_t id = *id_res;

        if (id == 7) {
            // ID_END
            break;
        } else if (id == 0) {
            // ID_SCE (Single Channel Element)
            auto tag = reader.read_bits(4);
            if (!tag) return tag.error();
            channels_ = 1;
            layout_ = ChannelLayout::Mono;
            auto res = decode_sce(reader, pcm_out);
            if (!res) return res;
            break;
        } else if (id == 1) {
            // ID_CPE (Channel Pair Element)
            auto tag = reader.read_bits(4);
            if (!tag) return tag.error();
            channels_ = 2;
            layout_ = ChannelLayout::Stereo;
            auto res = decode_cpe(reader, pcm_out);
            if (!res) return res;
            break;
        } else if (id == 6) {
            // ID_FIL (Fill element)
            auto cnt_res = reader.read_bits(4);
            if (!cnt_res) break;
            uint32_t count = *cnt_res;
            if (count == 15) {
                auto esc_cnt = reader.read_bits(8);
                if (esc_cnt) count += *esc_cnt - 1;
            }
            if (reader.bits_left() < count * 8) break;
            reader.skip_bits(count * 8);
        } else {
            // Unhandled syntax element
            break;
        }
    }

    if (pcm_out.empty()) {
        // Synthesize silent 1024-sample frame if stream is truncated
        pcm_out.assign(1024 * channels_, 0.0f);
    }

    // Allocate decoded AudioFrame
    AudioFrame frame = AudioFrame::allocate(
        sample_rate_,
        channels_,
        AudioSampleFormat::F32LE,
        1024,
        layout_
    );
    frame.set_pts(pts);
    frame.set_dts(dts);
    std::memcpy(frame.data_mut(), pcm_out.data(), pcm_out.size() * sizeof(float));

    frame_queue_.push(std::move(frame));
    return {};
}

MediaResult<void> AacDecoder::decode_sce(BitReader& reader, std::vector<float>& pcm_out) {
    auto gain_res = reader.read_bits(8);
    if (!gain_res) return gain_res.error();
    uint8_t global_gain = static_cast<uint8_t>(*gain_res);

    auto ics_res = AacEntropyDecoder::decode_ics_info(reader);
    if (!ics_res) return ics_res.error();
    const auto& ics = *ics_res;

    uint8_t codebooks[8 * AAC_MAX_SFB];
    auto sec_res = AacEntropyDecoder::decode_section_data(reader, ics, codebooks);
    if (!sec_res) return sec_res.error();

    int16_t scalefactors[8 * AAC_MAX_SFB];
    auto sf_res = AacEntropyDecoder::decode_scalefactors(reader, ics, global_gain, codebooks, scalefactors);
    if (!sf_res) return sf_res.error();

    int32_t quantized[AAC_MAX_BINS];
    auto spec_res = AacEntropyDecoder::decode_spectral_data(reader, ics, codebooks, quantized);
    if (!spec_res) return spec_res.error();

    auto tns_res = AacTools::decode_tns_data(reader, ics);
    if (!tns_res) return tns_res.error();
    const auto& tns = *tns_res;

    float spectrum[AAC_MAX_BINS];
    AacEntropyDecoder::dequantize_spectrum(ics, quantized, scalefactors, spectrum);
    AacTools::apply_tns(ics, tns, spectrum, sample_rate_);

    pcm_out.resize(1024);
    imdct_engine_.process_channel(ics, spectrum, 0, pcm_out.data());
    return {};
}

MediaResult<void> AacDecoder::decode_cpe(BitReader& reader, std::vector<float>& pcm_out) {
    auto gain_res = reader.read_bits(8);
    if (!gain_res) return gain_res.error();
    uint8_t global_gain_l = static_cast<uint8_t>(*gain_res);

    auto cwin_res = reader.read_bit();
    if (!cwin_res) return cwin_res.error();
    bool common_window = (*cwin_res != 0);

    AacIcsInfo ics_l{}, ics_r{};
    uint8_t ms_mask[8 * AAC_MAX_SFB]{0};
    bool ms_mask_present = false;

    if (common_window) {
        auto ics_res = AacEntropyDecoder::decode_ics_info(reader);
        if (!ics_res) return ics_res.error();
        ics_l = *ics_res;
        ics_r = *ics_res;

        auto ms_res = reader.read_bits(2);
        if (!ms_res) return ms_res.error();
        uint32_t ms_mode = *ms_res;
        if (ms_mode == 1) {
            ms_mask_present = true;
            for (uint8_t g = 0; g < ics_l.num_window_groups; ++g) {
                for (uint8_t sfb = 0; sfb < ics_l.max_sfb; ++sfb) {
                    auto b = reader.read_bit();
                    if (!b) return b.error();
                    ms_mask[g * AAC_MAX_SFB + sfb] = static_cast<uint8_t>(*b);
                }
            }
        } else if (ms_mode == 2) {
            ms_mask_present = true;
            std::memset(ms_mask, 1, sizeof(ms_mask));
        }
    } else {
        auto ics_l_res = AacEntropyDecoder::decode_ics_info(reader);
        if (!ics_l_res) return ics_l_res.error();
        ics_l = *ics_l_res;

        auto ics_r_res = AacEntropyDecoder::decode_ics_info(reader);
        if (!ics_r_res) return ics_r_res.error();
        ics_r = *ics_r_res;
    }

    // Decode Channel L
    uint8_t codebooks_l[8 * AAC_MAX_SFB];
    auto sec_l_res = AacEntropyDecoder::decode_section_data(reader, ics_l, codebooks_l);
    if (!sec_l_res) return sec_l_res.error();

    int16_t sf_l[8 * AAC_MAX_SFB];
    auto sf_l_res = AacEntropyDecoder::decode_scalefactors(reader, ics_l, global_gain_l, codebooks_l, sf_l);
    if (!sf_l_res) return sf_l_res.error();

    int32_t q_l[AAC_MAX_BINS];
    auto spec_l_res = AacEntropyDecoder::decode_spectral_data(reader, ics_l, codebooks_l, q_l);
    if (!spec_l_res) return spec_l_res.error();

    // Decode Channel R
    auto gain_r_res = reader.read_bits(8);
    if (!gain_r_res) return gain_r_res.error();
    uint8_t global_gain_r = static_cast<uint8_t>(*gain_r_res);

    uint8_t codebooks_r[8 * AAC_MAX_SFB];
    auto sec_r_res = AacEntropyDecoder::decode_section_data(reader, ics_r, codebooks_r);
    if (!sec_r_res) return sec_r_res.error();

    int16_t sf_r[8 * AAC_MAX_SFB];
    auto sf_r_res = AacEntropyDecoder::decode_scalefactors(reader, ics_r, global_gain_r, codebooks_r, sf_r);
    if (!sf_r_res) return sf_r_res.error();

    int32_t q_r[AAC_MAX_BINS];
    auto spec_r_res = AacEntropyDecoder::decode_spectral_data(reader, ics_r, codebooks_r, q_r);
    if (!spec_r_res) return spec_r_res.error();

    // TNS decoding for both channels
    auto tns_l_res = AacTools::decode_tns_data(reader, ics_l);
    if (!tns_l_res) return tns_l_res.error();
    auto tns_r_res = AacTools::decode_tns_data(reader, ics_r);
    if (!tns_r_res) return tns_r_res.error();

    // De-quantize spectra
    float spec_l[AAC_MAX_BINS];
    float spec_r[AAC_MAX_BINS];
    AacEntropyDecoder::dequantize_spectrum(ics_l, q_l, sf_l, spec_l);
    AacEntropyDecoder::dequantize_spectrum(ics_r, q_r, sf_r, spec_r);

    // Apply Mid/Side Stereo if active
    if (common_window && ms_mask_present) {
        AacTools::apply_ms_stereo(ics_l, ms_mask, spec_l, spec_r, sample_rate_);
    }

    // Apply TNS
    AacTools::apply_tns(ics_l, *tns_l_res, spec_l, sample_rate_);
    AacTools::apply_tns(ics_r, *tns_r_res, spec_r, sample_rate_);

    // IMDCT + Overlap-Add
    float pcm_l[1024];
    float pcm_r[1024];
    imdct_engine_.process_channel(ics_l, spec_l, 0, pcm_l);
    imdct_engine_.process_channel(ics_r, spec_r, 1, pcm_r);

    // Interleave stereo output
    pcm_out.resize(2048);
    for (size_t i = 0; i < 1024; ++i) {
        pcm_out[2 * i + 0] = pcm_l[i];
        pcm_out[2 * i + 1] = pcm_r[i];
    }

    return {};
}

MediaResult<void> AacDecoder::receive_frame(AudioFrame& frame) {
    if (frame_queue_.empty()) {
        return MediaError::NeedMoreData;
    }
    frame = std::move(frame_queue_.front());
    frame_queue_.pop();
    return {};
}

} // namespace atum
