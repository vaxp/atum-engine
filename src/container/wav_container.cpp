#include "atum/container/wav_container.hpp"
#include <algorithm>

namespace atum {

MediaResult<void> WavDemuxer::open(std::shared_ptr<IoReader> reader) {
    if (!reader) return MediaError::IoError;
    reader_ = std::move(reader);
    streams_.clear();

    auto riff_tag = reader_->read_u32_be();
    if (!riff_tag.is_ok() || *riff_tag != make_fourcc('R', 'I', 'F', 'F')) {
        return MediaError::InvalidData;
    }

    auto riff_size = reader_->read_u32_le();
    if (!riff_size.is_ok()) return riff_size.error();

    auto wave_tag = reader_->read_u32_be();
    if (!wave_tag.is_ok() || *wave_tag != make_fourcc('W', 'A', 'V', 'E')) {
        return MediaError::InvalidData;
    }

    bool fmt_found = false;
    uint16_t format_tag = 1;
    uint16_t channels = 2;
    uint32_t sample_rate = 44100;
    uint16_t bits_per_sample = 16;

    while (reader_->remaining() > 8) {
        auto chunk_id = reader_->read_u32_be();
        if (!chunk_id.is_ok()) break;

        auto chunk_sz = reader_->read_u32_le();
        if (!chunk_sz.is_ok()) break;

        if (*chunk_id == make_fourcc('f', 'm', 't', ' ')) {
            if (*chunk_sz < 16) return MediaError::InvalidData;

            auto ftag = reader_->read_u16_le();
            auto ch = reader_->read_u16_le();
            auto srate = reader_->read_u32_le();
            auto brate = reader_->read_u32_le();
            (void)brate;
            auto balign = reader_->read_u16_le();
            auto bps = reader_->read_u16_le();

            if (!ftag.is_ok() || !ch.is_ok() || !srate.is_ok() || !bps.is_ok()) {
                return MediaError::InvalidData;
            }

            format_tag = *ftag;
            channels = *ch;
            sample_rate = *srate;
            block_align_ = *balign;
            bits_per_sample = *bps;
            fmt_found = true;

            if (*chunk_sz > 16) {
                reader_->skip(*chunk_sz - 16);
            }
        } else if (*chunk_id == make_fourcc('d', 'a', 't', 'a')) {
            if (!fmt_found) return MediaError::InvalidData;
            data_start_offset_ = reader_->tell();
            data_size_ = *chunk_sz;
            current_data_offset_ = 0;
            break;
        } else {
            // Skip unhandled chunk (e.g. LIST, JUNK, etc.)
            reader_->skip(*chunk_sz);
        }
    }

    if (!fmt_found || data_size_ <= 0) {
        return MediaError::InvalidData;
    }

    AudioSampleFormat audio_fmt = AudioSampleFormat::S16LE;
    CodecId cid = CodecId::PCM_S16LE;

    if (bits_per_sample == 8) {
        audio_fmt = AudioSampleFormat::U8;
    } else if (bits_per_sample == 16) {
        audio_fmt = AudioSampleFormat::S16LE;
        cid = CodecId::PCM_S16LE;
    } else if (bits_per_sample == 24) {
        audio_fmt = AudioSampleFormat::S24LE;
        cid = CodecId::PCM_S24LE;
    } else if (bits_per_sample == 32) {
        if (format_tag == 3) {
            audio_fmt = AudioSampleFormat::F32LE;
            cid = CodecId::PCM_F32LE;
        } else {
            audio_fmt = AudioSampleFormat::S32LE;
            cid = CodecId::PCM_S32LE;
        }
    }

    Stream s;
    s.index = 0;
    s.codec_params = CodecParameters::create_audio(
        cid,
        sample_rate,
        channels,
        audio_fmt,
        channels == 1 ? ChannelLayout::Mono : ChannelLayout::Stereo
    );
    s.timebase = Rational(1, static_cast<int32_t>(sample_rate));
    s.nb_frames = block_align_ > 0 ? data_size_ / block_align_ : 0;
    s.duration = s.nb_frames;

    duration_ = sample_rate > 0 ? (s.nb_frames * 1000000LL) / sample_rate : 0;
    streams_.push_back(s);
    current_pts_ = 0;

    return MediaResult<void>::ok();
}

MediaResult<void> WavDemuxer::read_packet(Packet& packet) {
    if (current_data_offset_ >= data_size_) {
        return MediaError::EndOfStream;
    }

    size_t chunk_bytes = 4096 * block_align_;
    size_t remaining_bytes = static_cast<size_t>(data_size_ - current_data_offset_);
    size_t to_read = std::min(chunk_bytes, remaining_bytes);

    packet = Packet::allocate(to_read);
    auto res = reader_->read_exact(packet.data_mut(), to_read);
    if (!res.is_ok()) return res.error();

    size_t samples_read = block_align_ > 0 ? to_read / block_align_ : to_read;
    packet.set_pts(current_pts_);
    packet.set_dts(current_pts_);
    packet.set_duration(static_cast<int64_t>(samples_read));
    packet.set_stream_index(0);
    packet.set_keyframe(true);

    current_data_offset_ += to_read;
    current_pts_ += samples_read;

    return MediaResult<void>::ok();
}

MediaResult<void> WavDemuxer::seek(int64_t timestamp, int32_t /*stream_index*/) {
    if (block_align_ == 0 || streams_.empty()) return MediaError::InvalidState;

    int64_t target_sample = timestamp;
    int64_t target_byte = target_sample * block_align_;
    if (target_byte > data_size_) target_byte = data_size_;
    if (target_byte < 0) target_byte = 0;

    auto res = reader_->seek(data_start_offset_ + target_byte, SeekOrigin::Begin);
    if (!res.is_ok()) return res.error();

    current_data_offset_ = target_byte;
    current_pts_ = target_sample;
    return MediaResult<void>::ok();
}

// --- WavMuxer ---

MediaResult<void> WavMuxer::open(std::shared_ptr<IoWriter> writer) {
    if (!writer) return MediaError::IoError;
    writer_ = std::move(writer);
    header_written_ = false;
    total_data_bytes_ = 0;
    return MediaResult<void>::ok();
}

MediaResult<int32_t> WavMuxer::add_stream(const CodecParameters& params, Rational timebase) {
    if (header_written_) return MediaError::InvalidState;
    params_ = params;
    timebase_ = timebase;
    return int32_t{0};
}

MediaResult<void> WavMuxer::write_header() {
    if (!writer_) return MediaError::IoError;

    // RIFF header placeholder
    writer_->write_u32_be(make_fourcc('R', 'I', 'F', 'F'));
    writer_->write_u32_le(36); // Initial placeholder
    writer_->write_u32_be(make_fourcc('W', 'A', 'V', 'E'));

    // fmt  chunk
    writer_->write_u32_be(make_fourcc('f', 'm', 't', ' '));
    writer_->write_u32_le(16); // fmt chunk size

    uint16_t format_tag = (params_.audio_format == AudioSampleFormat::F32LE) ? 3 : 1;
    uint16_t channels = static_cast<uint16_t>(params_.channels);
    uint32_t sample_rate = params_.sample_rate;
    uint16_t bits_per_sample = static_cast<uint16_t>(audio_sample_format_bytes(params_.audio_format) * 8);
    uint16_t block_align = channels * (bits_per_sample / 8);
    uint32_t byte_rate = sample_rate * block_align;

    writer_->write_u16_le(format_tag);
    writer_->write_u16_le(channels);
    writer_->write_u32_le(sample_rate);
    writer_->write_u32_le(byte_rate);
    writer_->write_u16_le(block_align);
    writer_->write_u16_le(bits_per_sample);

    // data chunk header placeholder
    writer_->write_u32_be(make_fourcc('d', 'a', 't', 'a'));
    writer_->write_u32_le(0); // Initial 0 bytes

    header_written_ = true;
    total_data_bytes_ = 0;
    return MediaResult<void>::ok();
}

MediaResult<void> WavMuxer::write_packet(const Packet& packet) {
    if (!header_written_) {
        auto res = write_header();
        if (!res.is_ok()) return res;
    }

    auto res = writer_->write_exact(packet.data(), packet.size());
    if (!res.is_ok()) return res;

    total_data_bytes_ += static_cast<uint32_t>(packet.size());
    return MediaResult<void>::ok();
}

MediaResult<void> WavMuxer::write_trailer() {
    if (!writer_ || !header_written_) return MediaError::InvalidState;

    // Update RIFF file size (at offset 4): 36 + total_data_bytes_
    writer_->seek(4, SeekOrigin::Begin);
    writer_->write_u32_le(36 + total_data_bytes_);

    // Update data chunk size (at offset 40)
    writer_->seek(40, SeekOrigin::Begin);
    writer_->write_u32_le(total_data_bytes_);

    writer_->flush();
    return MediaResult<void>::ok();
}

} // namespace atum
