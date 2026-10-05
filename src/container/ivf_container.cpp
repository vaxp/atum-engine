#include "atum/container/ivf_container.hpp"

namespace atum {

MediaResult<void> IvfDemuxer::open(std::shared_ptr<IoReader> reader) {
    if (!reader) return MediaError::IoError;
    reader_ = std::move(reader);
    streams_.clear();

    auto magic = reader_->read_u32_be();
    if (!magic.is_ok() || *magic != make_fourcc('D', 'K', 'I', 'F')) {
        return MediaError::InvalidData;
    }

    auto version = reader_->read_u16_le();
    auto hdr_len = reader_->read_u16_le();
    auto fourcc = reader_->read_u32_le();
    auto width = reader_->read_u16_le();
    auto height = reader_->read_u16_le();
    auto tb_den = reader_->read_u32_le();
    auto tb_num = reader_->read_u32_le();
    auto num_frames = reader_->read_u32_le();
    auto unused = reader_->read_u32_le();
    (void)unused;

    if (!version.is_ok() || !hdr_len.is_ok() || !fourcc.is_ok() || !width.is_ok() ||
        !height.is_ok() || !tb_den.is_ok() || !tb_num.is_ok() || !num_frames.is_ok()) {
        return MediaError::InvalidData;
    }

    num_frames_ = *num_frames;
    current_frame_ = 0;

    CodecId cid = CodecId::None;
    uint32_t fcc = *fourcc;
    if (fcc == make_fourcc('Q', 'O', 'I', '0') || fcc == make_fourcc('q', 'o', 'i', 'f')) {
        cid = CodecId::QOI_VIDEO;
    } else if (fcc == make_fourcc('V', 'P', '8', '0')) {
        cid = CodecId::VP8;
    } else if (fcc == make_fourcc('V', 'P', '9', '0')) {
        cid = CodecId::VP9;
    } else if (fcc == make_fourcc('A', 'V', '0', '1')) {
        cid = CodecId::AV1;
    } else if (fcc == make_fourcc('J', 'P', 'E', 'G') || fcc == make_fourcc('M', 'J', 'P', 'G')) {
        cid = CodecId::MJPEG;
    }

    Rational tb(static_cast<int32_t>(*tb_num), static_cast<int32_t>(*tb_den));

    Stream s;
    s.index = 0;
    s.codec_params = CodecParameters::create_video(
        cid,
        *width,
        *height,
        PixelFormat::RGBA32,
        tb.invert()
    );
    s.timebase = tb;
    s.nb_frames = num_frames_;
    s.duration = num_frames_;

    duration_ = static_cast<int64_t>(Rational::to_seconds(s.duration, tb) * 1000000.0);
    streams_.push_back(s);

    if (*hdr_len > 32) {
        reader_->skip(*hdr_len - 32);
    }

    return MediaResult<void>::ok();
}

MediaResult<void> IvfDemuxer::read_packet(Packet& packet) {
    if (reader_->remaining() < 12) {
        return MediaError::EndOfStream;
    }

    auto frame_sz = reader_->read_u32_le();
    auto pts = reader_->read_u64_le();
    if (!frame_sz.is_ok() || !pts.is_ok()) return MediaError::EndOfStream;

    if (*frame_sz == 0) return MediaError::InvalidData;

    packet = Packet::allocate(*frame_sz);
    auto res = reader_->read_exact(packet.data_mut(), *frame_sz);
    if (!res.is_ok()) return res.error();

    packet.set_pts(static_cast<int64_t>(*pts));
    packet.set_dts(static_cast<int64_t>(*pts));
    packet.set_duration(1);
    packet.set_stream_index(0);
    packet.set_keyframe(true);

    current_frame_++;
    return MediaResult<void>::ok();
}

MediaResult<void> IvfDemuxer::seek(int64_t /*timestamp*/, int32_t /*stream_index*/) {
    // Reset to beginning of frames (offset 32)
    auto res = reader_->seek(32, SeekOrigin::Begin);
    if (!res.is_ok()) return res.error();
    current_frame_ = 0;
    return MediaResult<void>::ok();
}

// --- IvfMuxer ---

MediaResult<void> IvfMuxer::open(std::shared_ptr<IoWriter> writer) {
    if (!writer) return MediaError::IoError;
    writer_ = std::move(writer);
    header_written_ = false;
    frame_count_ = 0;
    return MediaResult<void>::ok();
}

MediaResult<int32_t> IvfMuxer::add_stream(const CodecParameters& params, Rational timebase) {
    if (header_written_) return MediaError::InvalidState;
    params_ = params;
    timebase_ = timebase;
    return int32_t{0};
}

MediaResult<void> IvfMuxer::write_header() {
    if (!writer_) return MediaError::IoError;

    writer_->write_u32_be(make_fourcc('D', 'K', 'I', 'F'));
    writer_->write_u16_le(0);  // version 0
    writer_->write_u16_le(32); // header length

    uint32_t fcc = make_fourcc('Q', 'O', 'I', '0');
    if (params_.codec_id == CodecId::MJPEG) {
        fcc = make_fourcc('M', 'J', 'P', 'G');
    } else if (params_.codec_id == CodecId::VP8) {
        fcc = make_fourcc('V', 'P', '8', '0');
    } else if (params_.codec_id == CodecId::VP9) {
        fcc = make_fourcc('V', 'P', '9', '0');
    } else if (params_.codec_id == CodecId::AV1) {
        fcc = make_fourcc('A', 'V', '0', '1');
    }

    writer_->write_u32_le(fcc);
    writer_->write_u16_le(static_cast<uint16_t>(params_.width));
    writer_->write_u16_le(static_cast<uint16_t>(params_.height));
    writer_->write_u32_le(static_cast<uint32_t>(timebase_.den));
    writer_->write_u32_le(static_cast<uint32_t>(timebase_.num));
    writer_->write_u32_le(0); // placeholder for frame count
    writer_->write_u32_le(0); // unused

    header_written_ = true;
    frame_count_ = 0;
    return MediaResult<void>::ok();
}

MediaResult<void> IvfMuxer::write_packet(const Packet& packet) {
    if (!header_written_) {
        auto res = write_header();
        if (!res.is_ok()) return res;
    }

    writer_->write_u32_le(static_cast<uint32_t>(packet.size()));
    writer_->write_u64_le(static_cast<uint64_t>(packet.pts() >= 0 ? packet.pts() : frame_count_));
    auto res = writer_->write_exact(packet.data(), packet.size());
    if (!res.is_ok()) return res;

    frame_count_++;
    return MediaResult<void>::ok();
}

MediaResult<void> IvfMuxer::write_trailer() {
    if (!writer_ || !header_written_) return MediaError::InvalidState;

    // Update frame count at byte offset 24
    writer_->seek(24, SeekOrigin::Begin);
    writer_->write_u32_le(frame_count_);
    writer_->flush();
    return MediaResult<void>::ok();
}

} // namespace atum
