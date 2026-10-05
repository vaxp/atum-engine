#include "atum/container/mkv/mkv_muxer.hpp"
#include "atum/io/memory_stream.hpp"
#include <algorithm>
#include <vector>

namespace atum {

std::string MkvMuxer::map_to_mkv_codec_id(CodecId id) noexcept {
    switch (id) {
        case CodecId::H264:   return "V_MPEG4/ISO/AVC";
        case CodecId::HEVC:   return "V_MPEGH/ISO/HEVC";
        case CodecId::VP9:    return "V_VP9";
        case CodecId::AV1:    return "V_AV1";
        case CodecId::AAC:    return "A_AAC";
        case CodecId::Opus:   return "A_OPUS";
        case CodecId::FLAC:   return "A_FLAC";
        case CodecId::Vorbis: return "A_VORBIS";
        default:              return "V_UNSUPPORTED";
    }
}

MediaResult<void> MkvMuxer::open(std::shared_ptr<IoWriter> writer) {
    if (!writer) return MediaError::IoError;
    writer_ = std::move(writer);
    tracks_.clear();
    cues_.clear();
    cluster_active_ = false;
    segment_data_start_ = 0;
    current_cluster_start_ = 0;
    current_cluster_time_ms_ = 0;
    max_pts_ms_ = 0;
    return MediaResult<void>::ok();
}

MediaResult<int32_t> MkvMuxer::add_stream(const CodecParameters& params, Rational timebase) {
    int32_t idx = static_cast<int32_t>(tracks_.size());
    TrackInfo ti;
    ti.stream_index = idx;
    ti.params = params;
    ti.timebase = timebase;
    ti.track_number = static_cast<uint64_t>(idx + 1);
    ti.codec_id = map_to_mkv_codec_id(params.codec_id);

    // Auto-detect DocType
    if (params.codec_id == CodecId::H264 || params.codec_id == CodecId::HEVC ||
        params.codec_id == CodecId::AAC || params.codec_id == CodecId::FLAC) {
        doc_type_ = "matroska";
    }

    tracks_.push_back(std::move(ti));
    return idx;
}

MediaResult<void> MkvMuxer::write_header() {
    if (!writer_) return MediaError::IoError;

    // 1. EBML Header
    MemoryWriter ebml_mw;
    Ebml::write_uint_element(ebml_mw, ebml_id::EBMLVersion, 1);
    Ebml::write_uint_element(ebml_mw, ebml_id::EBMLReadVersion, 1);
    Ebml::write_uint_element(ebml_mw, ebml_id::EBMLMaxIDLength, 4);
    Ebml::write_uint_element(ebml_mw, ebml_id::EBMLMaxSizeLength, 8);
    Ebml::write_string_element(ebml_mw, ebml_id::DocType, doc_type_);
    Ebml::write_uint_element(ebml_mw, ebml_id::DocTypeVersion, 4);
    Ebml::write_uint_element(ebml_mw, ebml_id::DocTypeReadVersion, 2);

    auto res = Ebml::write_master_header(*writer_, ebml_id::EBML, ebml_mw.buffer().size());
    if (!res.is_ok()) return res;
    res = writer_->write_exact(ebml_mw.buffer().data(), ebml_mw.buffer().size());
    if (!res.is_ok()) return res;

    // 2. Segment Header (Unknown size: 8 bytes all ones data mask)
    uint8_t seg_id_buf[4];
    size_t seg_id_len = Ebml::write_element_id(seg_id_buf, ebml_id::Segment);
    res = writer_->write_exact(seg_id_buf, seg_id_len);
    if (!res.is_ok()) return res;

    uint8_t unk_size[8] = {0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    res = writer_->write_exact(unk_size, 8);
    if (!res.is_ok()) return res;

    segment_data_start_ = writer_->tell();

    // 3. Info Element
    MemoryWriter info_mw;
    Ebml::write_uint_element(info_mw, ebml_id::TimestampScale, 1000000); // 1 ms
    Ebml::write_string_element(info_mw, ebml_id::MuxingApp, "Nisaba Sovereign MkvMuxer");
    Ebml::write_string_element(info_mw, ebml_id::WritingApp, "Nisaba 2D Graphics Engine");

    res = Ebml::write_master_header(*writer_, ebml_id::Info, info_mw.buffer().size());
    if (!res.is_ok()) return res;
    res = writer_->write_exact(info_mw.buffer().data(), info_mw.buffer().size());
    if (!res.is_ok()) return res;

    // 4. Tracks Element
    MemoryWriter tracks_mw;
    for (const auto& trk : tracks_) {
        MemoryWriter trk_entry_mw;
        Ebml::write_uint_element(trk_entry_mw, ebml_id::TrackNumber, trk.track_number);
        Ebml::write_uint_element(trk_entry_mw, ebml_id::TrackUID, trk.track_number + 1000);
        uint64_t type_val = (trk.params.media_type == MediaType::Video) ? 1 : 2;
        Ebml::write_uint_element(trk_entry_mw, ebml_id::TrackType, type_val);
        Ebml::write_string_element(trk_entry_mw, ebml_id::CodecID, trk.codec_id);

        if (!trk.params.extradata.empty()) {
            Ebml::write_binary_element(trk_entry_mw, ebml_id::CodecPrivate, trk.params.extradata);
        }

        if (trk.params.media_type == MediaType::Video) {
            MemoryWriter video_mw;
            Ebml::write_uint_element(video_mw, ebml_id::PixelWidth, trk.params.width > 0 ? trk.params.width : 1920);
            Ebml::write_uint_element(video_mw, ebml_id::PixelHeight, trk.params.height > 0 ? trk.params.height : 1080);
            Ebml::write_master_header(trk_entry_mw, ebml_id::Video, video_mw.buffer().size());
            trk_entry_mw.write_exact(video_mw.buffer().data(), video_mw.buffer().size());
        } else if (trk.params.media_type == MediaType::Audio) {
            MemoryWriter audio_mw;
            Ebml::write_float_element(audio_mw, ebml_id::SamplingFrequency,
                trk.params.sample_rate > 0 ? static_cast<double>(trk.params.sample_rate) : 48000.0);
            Ebml::write_uint_element(audio_mw, ebml_id::Channels,
                trk.params.channels > 0 ? trk.params.channels : 2);
            Ebml::write_uint_element(audio_mw, ebml_id::BitDepth, 16);
            Ebml::write_master_header(trk_entry_mw, ebml_id::Audio, audio_mw.buffer().size());
            audio_mw.flush();
            trk_entry_mw.write_exact(audio_mw.buffer().data(), audio_mw.buffer().size());
        }

        Ebml::write_master_header(tracks_mw, ebml_id::TrackEntry, trk_entry_mw.buffer().size());
        tracks_mw.write_exact(trk_entry_mw.buffer().data(), trk_entry_mw.buffer().size());
    }

    res = Ebml::write_master_header(*writer_, ebml_id::Tracks, tracks_mw.buffer().size());
    if (!res.is_ok()) return res;
    res = writer_->write_exact(tracks_mw.buffer().data(), tracks_mw.buffer().size());
    if (!res.is_ok()) return res;

    return writer_->flush();
}

MediaResult<void> MkvMuxer::start_new_cluster(int64_t time_ms) {
    if (cluster_active_) {
        // Patch previous cluster's 8-byte unknown size if seekable
        int64_t current_pos = writer_->tell();
        int64_t prev_cluster_size = current_pos - (current_cluster_start_ + 4 + 8);
        if (prev_cluster_size >= 0) {
            auto s_res = writer_->seek(current_cluster_start_ + 4, SeekOrigin::Begin);
            if (s_res.is_ok()) {
                uint8_t sz_buf[8];
                sz_buf[0] = 0x01 | static_cast<uint8_t>((prev_cluster_size >> 56) & 0x7F);
                sz_buf[1] = static_cast<uint8_t>((prev_cluster_size >> 48) & 0xFF);
                sz_buf[2] = static_cast<uint8_t>((prev_cluster_size >> 40) & 0xFF);
                sz_buf[3] = static_cast<uint8_t>((prev_cluster_size >> 32) & 0xFF);
                sz_buf[4] = static_cast<uint8_t>((prev_cluster_size >> 24) & 0xFF);
                sz_buf[5] = static_cast<uint8_t>((prev_cluster_size >> 16) & 0xFF);
                sz_buf[6] = static_cast<uint8_t>((prev_cluster_size >> 8) & 0xFF);
                sz_buf[7] = static_cast<uint8_t>(prev_cluster_size & 0xFF);
                writer_->write_exact(sz_buf, 8);
                writer_->seek(current_pos, SeekOrigin::Begin);
            }
        }
    }

    current_cluster_start_ = writer_->tell();
    current_cluster_time_ms_ = time_ms;
    cluster_active_ = true;

    // Cluster with placeholder size
    uint8_t cl_id[4];
    size_t cl_id_len = Ebml::write_element_id(cl_id, ebml_id::Cluster);
    auto res = writer_->write_exact(cl_id, cl_id_len);
    if (!res.is_ok()) return res;

    uint8_t unk_size[8] = {0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    res = writer_->write_exact(unk_size, 8);
    if (!res.is_ok()) return res;

    // Cluster Timestamp
    return Ebml::write_uint_element(*writer_, ebml_id::Timestamp, static_cast<uint64_t>(time_ms));
}

MediaResult<void> MkvMuxer::write_packet(const Packet& packet) {
    if (!writer_) return MediaError::IoError;
    if (packet.stream_index() < 0 || static_cast<size_t>(packet.stream_index()) >= tracks_.size()) {
        return MediaError::InvalidData;
    }

    const auto& trk = tracks_[packet.stream_index()];
    int64_t pts_ms = packet.pts() >= 0 ? rescale_timestamp(packet.pts(), trk.timebase, Rational{1, 1000}) : current_cluster_time_ms_;
    max_pts_ms_ = std::max(max_pts_ms_, pts_ms);

    if (!cluster_active_ || (pts_ms - current_cluster_time_ms_ >= 1000) || (packet.is_keyframe() && pts_ms > current_cluster_time_ms_)) {
        auto res = start_new_cluster(pts_ms);
        if (!res.is_ok()) return res;
    }

    if (packet.is_keyframe()) {
        cues_.push_back(CueEntry{
            .time_ms = pts_ms,
            .track_number = trk.track_number,
            .cluster_position = current_cluster_start_ - segment_data_start_
        });
    }

    // Prepare SimpleBlock payload
    // Track number (VINT)
    uint8_t trk_vint[8];
    size_t trk_vint_len = Ebml::write_vint_size(trk_vint, trk.track_number);

    int16_t rel_ts = static_cast<int16_t>(pts_ms - current_cluster_time_ms_);
    uint8_t ts_bytes[2] = {static_cast<uint8_t>(rel_ts >> 8), static_cast<uint8_t>(rel_ts & 0xFF)};

    uint8_t flags = packet.is_keyframe() ? 0x80 : 0x00; // Bit 7: Keyframe, Bits 2-1: 0 (No lacing)

    uint64_t block_payload_size = trk_vint_len + 2 + 1 + packet.size();

    // Write SimpleBlock master/element header
    auto res = Ebml::write_master_header(*writer_, ebml_id::SimpleBlock, block_payload_size);
    if (!res.is_ok()) return res;

    res = writer_->write_exact(trk_vint, trk_vint_len);
    if (!res.is_ok()) return res;

    res = writer_->write_exact(ts_bytes, 2);
    if (!res.is_ok()) return res;

    res = writer_->write_u8(flags);
    if (!res.is_ok()) return res;

    return writer_->write_exact(packet.data(), packet.size());
}

MediaResult<void> MkvMuxer::write_trailer() {
    if (!writer_) return MediaError::IoError;

    if (cluster_active_) {
        // Patch last cluster's size
        int64_t current_pos = writer_->tell();
        int64_t last_cluster_size = current_pos - (current_cluster_start_ + 4 + 8);
        if (last_cluster_size >= 0) {
            auto s_res = writer_->seek(current_cluster_start_ + 4, SeekOrigin::Begin);
            if (s_res.is_ok()) {
                uint8_t sz_buf[8];
                sz_buf[0] = 0x01 | static_cast<uint8_t>((last_cluster_size >> 56) & 0x7F);
                sz_buf[1] = static_cast<uint8_t>((last_cluster_size >> 48) & 0xFF);
                sz_buf[2] = static_cast<uint8_t>((last_cluster_size >> 40) & 0xFF);
                sz_buf[3] = static_cast<uint8_t>((last_cluster_size >> 32) & 0xFF);
                sz_buf[4] = static_cast<uint8_t>((last_cluster_size >> 24) & 0xFF);
                sz_buf[5] = static_cast<uint8_t>((last_cluster_size >> 16) & 0xFF);
                sz_buf[6] = static_cast<uint8_t>((last_cluster_size >> 8) & 0xFF);
                sz_buf[7] = static_cast<uint8_t>(last_cluster_size & 0xFF);
                writer_->write_exact(sz_buf, 8);
                writer_->seek(current_pos, SeekOrigin::Begin);
            }
        }
        cluster_active_ = false;
    }

    // Write Cues element for random access
    if (!cues_.empty()) {
        MemoryWriter cues_mw;
        for (const auto& cue : cues_) {
            MemoryWriter cp_mw;
            Ebml::write_uint_element(cp_mw, ebml_id::CueTime, static_cast<uint64_t>(cue.time_ms));

            MemoryWriter ctp_mw;
            Ebml::write_uint_element(ctp_mw, ebml_id::CueTrack, cue.track_number);
            Ebml::write_uint_element(ctp_mw, ebml_id::CueClusterPosition, static_cast<uint64_t>(cue.cluster_position));

            Ebml::write_master_header(cp_mw, ebml_id::CueTrackPositions, ctp_mw.buffer().size());
            cp_mw.write_exact(ctp_mw.buffer().data(), ctp_mw.buffer().size());

            Ebml::write_master_header(cues_mw, ebml_id::CuePoint, cp_mw.buffer().size());
            cues_mw.write_exact(cp_mw.buffer().data(), cp_mw.buffer().size());
        }

        auto res = Ebml::write_master_header(*writer_, ebml_id::Cues, cues_mw.buffer().size());
        if (!res.is_ok()) return res;
        res = writer_->write_exact(cues_mw.buffer().data(), cues_mw.buffer().size());
        if (!res.is_ok()) return res;
    }

    return writer_->flush();
}

} // namespace atum
