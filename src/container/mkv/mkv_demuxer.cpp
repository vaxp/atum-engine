#include "atum/container/mkv/mkv_demuxer.hpp"
#include <algorithm>
#include <cstring>

namespace atum {

CodecId MkvDemuxer::map_codec_id(std::string_view mkv_codec) noexcept {
    if (mkv_codec == "V_MPEG4/ISO/AVC") return CodecId::H264;
    if (mkv_codec == "V_MPEGH/ISO/HEVC") return CodecId::HEVC;
    if (mkv_codec == "V_VP9") return CodecId::VP9;
    if (mkv_codec == "V_AV1") return CodecId::AV1;
    if (mkv_codec == "A_AAC") return CodecId::AAC;
    if (mkv_codec == "A_OPUS") return CodecId::Opus;
    if (mkv_codec == "A_FLAC") return CodecId::FLAC;
    if (mkv_codec == "A_VORBIS") return CodecId::Vorbis;
    return CodecId::Unknown;
}

MediaResult<void> MkvDemuxer::open(std::shared_ptr<IoReader> reader) {
    if (!reader) return MediaError::IoError;
    reader_ = std::move(reader);
    streams_.clear();
    tracks_.clear();
    track_number_to_stream_idx_.clear();
    cues_.clear();
    packet_queue_.clear();
    first_cluster_offset_ = 0;
    current_cluster_timecode_ = 0;

    auto hdr_res = Ebml::read_element_header(*reader_);
    if (!hdr_res.is_ok()) return hdr_res.error();

    if (hdr_res->id != ebml_id::EBML) {
        return MediaError::InvalidData;
    }

    auto res = parse_ebml_header(*hdr_res);
    if (!res.is_ok()) return res;

    // Next element must be Segment
    auto seg_hdr = Ebml::read_element_header(*reader_);
    if (!seg_hdr.is_ok()) return seg_hdr.error();
    if (seg_hdr->id != ebml_id::Segment) {
        return MediaError::InvalidData;
    }

    res = parse_segment(*seg_hdr);
    if (!res.is_ok()) return res;

    if (first_cluster_offset_ > 0) {
        reader_->seek(first_cluster_offset_, SeekOrigin::Begin);
    }

    return MediaResult<void>::ok();
}

MediaResult<void> MkvDemuxer::parse_ebml_header(const EbmlElementHeader& hdr) {
    int64_t end = hdr.data_offset + static_cast<int64_t>(hdr.size);
    while (reader_->tell() < end) {
        auto el = Ebml::read_element_header(*reader_);
        if (!el.is_ok()) break;

        if (el->id == ebml_id::DocType) {
            auto dt = Ebml::read_string(*reader_, static_cast<size_t>(el->size));
            if (dt.is_ok()) doc_type_ = *dt;
        } else {
            reader_->skip(static_cast<int64_t>(el->size));
        }
    }
    reader_->seek(end, SeekOrigin::Begin);
    return MediaResult<void>::ok();
}

MediaResult<void> MkvDemuxer::parse_segment(const EbmlElementHeader& hdr) {
    segment_data_start_ = hdr.data_offset;
    segment_data_end_ = (hdr.size == EBML_UNKNOWN_SIZE) ? -1 : (segment_data_start_ + static_cast<int64_t>(hdr.size));

    // Walk child elements
    while (segment_data_end_ < 0 || reader_->tell() < segment_data_end_) {
        if (reader_->remaining() == 0) break;
        int64_t el_start = reader_->tell();
        auto el = Ebml::read_element_header(*reader_);
        if (!el.is_ok()) break;

        int64_t el_data_end = el->data_offset + static_cast<int64_t>(el->size);

        if (el->id == ebml_id::SeekHead) {
            parse_seek_head(el_data_end);
        } else if (el->id == ebml_id::Info) {
            parse_info(el_data_end);
        } else if (el->id == ebml_id::Tracks) {
            parse_tracks(el_data_end);
        } else if (el->id == ebml_id::Cues) {
            parse_cues(el_data_end);
        } else if (el->id == ebml_id::Cluster) {
            if (first_cluster_offset_ == 0) {
                first_cluster_offset_ = el_start;
            }
            if (reader_->size() <= 0 || el->size == EBML_UNKNOWN_SIZE) {
                // Non-seekable stream or unknown cluster size: stop scanning to start playback
                break;
            }
            // Seekable file with known size: skip cluster body to discover Cues at the tail
            reader_->seek(el_data_end, SeekOrigin::Begin);
        } else {
            reader_->seek(el_data_end, SeekOrigin::Begin);
        }
    }

    return MediaResult<void>::ok();
}

MediaResult<void> MkvDemuxer::parse_seek_head(int64_t end_offset) {
    while (reader_->tell() < end_offset) {
        auto el = Ebml::read_element_header(*reader_);
        if (!el.is_ok()) break;
        int64_t sub_end = el->data_offset + static_cast<int64_t>(el->size);

        if (el->id == ebml_id::Seek) {
            uint32_t seek_id = 0;
            uint64_t seek_pos = 0;
            while (reader_->tell() < sub_end) {
                auto sub = Ebml::read_element_header(*reader_);
                if (!sub.is_ok()) break;
                if (sub->id == ebml_id::SeekID) {
                    auto u_res = Ebml::read_uint(*reader_, static_cast<size_t>(sub->size));
                    if (u_res.is_ok()) seek_id = static_cast<uint32_t>(*u_res);
                } else if (sub->id == ebml_id::SeekPosition) {
                    auto p_res = Ebml::read_uint(*reader_, static_cast<size_t>(sub->size));
                    if (p_res.is_ok()) seek_pos = *p_res;
                } else {
                    reader_->skip(static_cast<int64_t>(sub->size));
                }
            }
            (void)seek_id;
            (void)seek_pos;
        } else {
            reader_->seek(sub_end, SeekOrigin::Begin);
        }
    }
    reader_->seek(end_offset, SeekOrigin::Begin);
    return MediaResult<void>::ok();
}

MediaResult<void> MkvDemuxer::parse_info(int64_t end_offset) {
    while (reader_->tell() < end_offset) {
        auto el = Ebml::read_element_header(*reader_);
        if (!el.is_ok()) break;

        if (el->id == ebml_id::TimestampScale) {
            auto u_res = Ebml::read_uint(*reader_, static_cast<size_t>(el->size));
            if (u_res.is_ok()) timecode_scale_ns_ = *u_res;
        } else if (el->id == ebml_id::Duration) {
            auto f_res = Ebml::read_float(*reader_, static_cast<size_t>(el->size));
            if (f_res.is_ok()) {
                double dur_ticks = *f_res;
                duration_ = static_cast<int64_t>(dur_ticks * static_cast<double>(timecode_scale_ns_) / 1000.0);
            }
        } else {
            reader_->skip(static_cast<int64_t>(el->size));
        }
    }
    reader_->seek(end_offset, SeekOrigin::Begin);
    return MediaResult<void>::ok();
}

MediaResult<void> MkvDemuxer::parse_tracks(int64_t end_offset) {
    while (reader_->tell() < end_offset) {
        auto el = Ebml::read_element_header(*reader_);
        if (!el.is_ok()) break;
        int64_t trk_end = el->data_offset + static_cast<int64_t>(el->size);

        if (el->id == ebml_id::TrackEntry) {
            MkvTrack trk;
            while (reader_->tell() < trk_end) {
                auto sub = Ebml::read_element_header(*reader_);
                if (!sub.is_ok()) break;
                int64_t sub_end = sub->data_offset + static_cast<int64_t>(sub->size);

                if (sub->id == ebml_id::TrackNumber) {
                    auto u = Ebml::read_uint(*reader_, static_cast<size_t>(sub->size));
                    if (u.is_ok()) trk.track_number = *u;
                } else if (sub->id == ebml_id::TrackUID) {
                    auto u = Ebml::read_uint(*reader_, static_cast<size_t>(sub->size));
                    if (u.is_ok()) trk.track_uid = *u;
                } else if (sub->id == ebml_id::TrackType) {
                    auto u = Ebml::read_uint(*reader_, static_cast<size_t>(sub->size));
                    if (u.is_ok()) trk.track_type = static_cast<MkvTrackType>(*u);
                } else if (sub->id == ebml_id::CodecID) {
                    auto s = Ebml::read_string(*reader_, static_cast<size_t>(sub->size));
                    if (s.is_ok()) trk.codec_id = *s;
                } else if (sub->id == ebml_id::CodecPrivate) {
                    trk.codec_private.resize(static_cast<size_t>(sub->size));
                    reader_->read_exact(trk.codec_private.data(), trk.codec_private.size());
                } else if (sub->id == ebml_id::DefaultDuration) {
                    auto u = Ebml::read_uint(*reader_, static_cast<size_t>(sub->size));
                    if (u.is_ok()) trk.default_duration_ns = *u;
                } else if (sub->id == ebml_id::Video) {
                    while (reader_->tell() < sub_end) {
                        auto vel = Ebml::read_element_header(*reader_);
                        if (!vel.is_ok()) break;
                        if (vel->id == ebml_id::PixelWidth) {
                            auto u = Ebml::read_uint(*reader_, static_cast<size_t>(vel->size));
                            if (u.is_ok()) trk.pixel_width = static_cast<uint32_t>(*u);
                        } else if (vel->id == ebml_id::PixelHeight) {
                            auto u = Ebml::read_uint(*reader_, static_cast<size_t>(vel->size));
                            if (u.is_ok()) trk.pixel_height = static_cast<uint32_t>(*u);
                        } else {
                            reader_->skip(static_cast<int64_t>(vel->size));
                        }
                    }
                } else if (sub->id == ebml_id::Audio) {
                    while (reader_->tell() < sub_end) {
                        auto ael = Ebml::read_element_header(*reader_);
                        if (!ael.is_ok()) break;
                        if (ael->id == ebml_id::SamplingFrequency) {
                            auto f = Ebml::read_float(*reader_, static_cast<size_t>(ael->size));
                            if (f.is_ok()) trk.sample_rate = *f;
                        } else if (ael->id == ebml_id::Channels) {
                            auto u = Ebml::read_uint(*reader_, static_cast<size_t>(ael->size));
                            if (u.is_ok()) trk.channels = static_cast<uint32_t>(*u);
                        } else if (ael->id == ebml_id::BitDepth) {
                            auto u = Ebml::read_uint(*reader_, static_cast<size_t>(ael->size));
                            if (u.is_ok()) trk.bit_depth = static_cast<uint32_t>(*u);
                        } else {
                            reader_->skip(static_cast<int64_t>(ael->size));
                        }
                    }
                } else {
                    reader_->seek(sub_end, SeekOrigin::Begin);
                }
            }

            // Build Stream descriptor
            int32_t stream_idx = static_cast<int32_t>(streams_.size());
            trk.stream.index = stream_idx;
            trk.stream.timebase = Rational{1, 1000}; // Milliseconds
            trk.stream.duration = duration_;

            auto& cp = trk.stream.codec_params;
            cp.codec_id = map_codec_id(trk.codec_id);
            cp.extradata = trk.codec_private;

            if (trk.track_type == MkvTrackType::Video) {
                cp.media_type = MediaType::Video;
                cp.width = trk.pixel_width;
                cp.height = trk.pixel_height;
            } else if (trk.track_type == MkvTrackType::Audio) {
                cp.media_type = MediaType::Audio;
                cp.sample_rate = static_cast<uint32_t>(trk.sample_rate > 0.0 ? trk.sample_rate : 48000.0);
                cp.channels = trk.channels > 0 ? trk.channels : 2;
                cp.audio_format = AudioSampleFormat::F32LE;
            }

            track_number_to_stream_idx_[trk.track_number] = stream_idx;
            streams_.push_back(trk.stream);
            tracks_.push_back(std::move(trk));
        } else {
            reader_->seek(trk_end, SeekOrigin::Begin);
        }
    }
    reader_->seek(end_offset, SeekOrigin::Begin);
    return MediaResult<void>::ok();
}

MediaResult<void> MkvDemuxer::parse_cues(int64_t end_offset) {
    while (reader_->tell() < end_offset) {
        auto el = Ebml::read_element_header(*reader_);
        if (!el.is_ok()) break;
        int64_t pt_end = el->data_offset + static_cast<int64_t>(el->size);

        if (el->id == ebml_id::CuePoint) {
            MkvCue cue;
            while (reader_->tell() < pt_end) {
                auto sub = Ebml::read_element_header(*reader_);
                if (!sub.is_ok()) break;
                int64_t sub_end = sub->data_offset + static_cast<int64_t>(sub->size);

                if (sub->id == ebml_id::CueTime) {
                    auto u = Ebml::read_uint(*reader_, static_cast<size_t>(sub->size));
                    if (u.is_ok()) cue.time_ms = static_cast<int64_t>(*u * timecode_scale_ns_ / 1000000);
                } else if (sub->id == ebml_id::CueTrackPositions) {
                    while (reader_->tell() < sub_end) {
                        auto ctel = Ebml::read_element_header(*reader_);
                        if (!ctel.is_ok()) break;
                        if (ctel->id == ebml_id::CueTrack) {
                            auto u = Ebml::read_uint(*reader_, static_cast<size_t>(ctel->size));
                            if (u.is_ok()) cue.track = *u;
                        } else if (ctel->id == ebml_id::CueClusterPosition) {
                            auto u = Ebml::read_uint(*reader_, static_cast<size_t>(ctel->size));
                            if (u.is_ok()) cue.cluster_offset = segment_data_start_ + static_cast<int64_t>(*u);
                        } else if (ctel->id == ebml_id::CueBlockNumber) {
                            auto u = Ebml::read_uint(*reader_, static_cast<size_t>(ctel->size));
                            if (u.is_ok()) cue.block_number = *u;
                        } else {
                            reader_->skip(static_cast<int64_t>(ctel->size));
                        }
                    }
                } else {
                    reader_->seek(sub_end, SeekOrigin::Begin);
                }
            }
            cues_.push_back(cue);
        } else {
            reader_->seek(pt_end, SeekOrigin::Begin);
        }
    }
    reader_->seek(end_offset, SeekOrigin::Begin);
    return MediaResult<void>::ok();
}

MediaResult<void> MkvDemuxer::read_packet(Packet& packet) {
    while (packet_queue_.empty()) {
        auto res = read_next_cluster_or_block();
        if (!res.is_ok()) return res;
    }

    packet = std::move(packet_queue_.front());
    packet_queue_.pop_front();
    return MediaResult<void>::ok();
}

MediaResult<void> MkvDemuxer::read_next_cluster_or_block() {
    while (true) {
        if (segment_data_end_ > 0 && reader_->tell() >= segment_data_end_) {
            return MediaError::EndOfStream;
        }
        if (reader_->remaining() < 4) {
            return MediaError::EndOfStream;
        }

        auto el = Ebml::read_element_header(*reader_);
        if (!el.is_ok()) return el.error();

        if (el->id == ebml_id::Cluster) {
            // Cluster container header
            continue;
        }

        if (el->id == ebml_id::Timestamp) {
            auto u = Ebml::read_uint(*reader_, static_cast<size_t>(el->size));
            if (u.is_ok()) {
                current_cluster_timecode_ = static_cast<int64_t>(*u);
            }
            continue;
        }

        if (el->id == ebml_id::SimpleBlock) {
            return parse_simple_block(static_cast<int64_t>(el->size));
        }

        if (el->id == ebml_id::BlockGroup) {
            return parse_block_group(static_cast<int64_t>(el->size));
        }

        // Other elements (e.g. PrevSize, Cues, Void)
        reader_->skip(static_cast<int64_t>(el->size));
    }
}

MediaResult<void> MkvDemuxer::parse_simple_block(int64_t payload_size) {
    int64_t start = reader_->tell();
    int64_t end = start + payload_size;

    // Track number (VINT)
    auto trk_res = Ebml::read_vint_size(*reader_);
    if (!trk_res.is_ok()) return trk_res.error();
    uint64_t track_num = *trk_res;

    // Relative timecode (signed 16-bit)
    auto rel_ts_res = reader_->read_u16_be();
    if (!rel_ts_res.is_ok()) return rel_ts_res.error();
    int16_t rel_ts = static_cast<int16_t>(*rel_ts_res);

    // Flags (1 byte)
    auto flags_res = reader_->read_u8();
    if (!flags_res.is_ok()) return flags_res.error();
    uint8_t flags = *flags_res;
    bool is_keyframe = (flags & 0x80) != 0;
    uint8_t lacing_mode = (flags >> 1) & 0x03;

    int32_t stream_idx = 0;
    auto it = track_number_to_stream_idx_.find(track_num);
    if (it != track_number_to_stream_idx_.end()) {
        stream_idx = static_cast<int32_t>(it->second);
    }

    int64_t block_time_ms = (current_cluster_timecode_ + rel_ts) * static_cast<int64_t>(timecode_scale_ns_) / 1000000;

    int64_t data_left = end - reader_->tell();
    if (data_left <= 0) return MediaResult<void>::ok();

    if (lacing_mode == 0) { // No lacing
        std::vector<uint8_t> buf(static_cast<size_t>(data_left));
        auto r_res = reader_->read_exact(buf.data(), buf.size());
        if (!r_res.is_ok()) return r_res;

        Packet pkt = Packet::from_bytes(buf);
        pkt.set_pts(block_time_ms);
        pkt.set_dts(block_time_ms);
        pkt.set_stream_index(stream_idx);
        pkt.set_keyframe(is_keyframe);
        packet_queue_.push_back(std::move(pkt));
    } else {
        // Lacing modes
        auto num_frames_res = reader_->read_u8();
        if (!num_frames_res.is_ok()) return num_frames_res.error();
        uint8_t num_frames = static_cast<uint8_t>(*num_frames_res + 1);

        std::vector<size_t> frame_sizes(num_frames, 0);

        if (lacing_mode == 1) { // Xiph lacing
            size_t total_lacings = 0;
            for (uint8_t i = 0; i < num_frames - 1; ++i) {
                size_t sz = 0;
                while (true) {
                    auto b = reader_->read_u8();
                    if (!b.is_ok()) return b.error();
                    sz += *b;
                    if (*b < 255) break;
                }
                frame_sizes[i] = sz;
                total_lacings += sz;
            }
            int64_t rem = end - reader_->tell();
            if (rem < 0 || static_cast<size_t>(rem) < total_lacings) return MediaError::InvalidData;
            frame_sizes[num_frames - 1] = static_cast<size_t>(rem);
        } else if (lacing_mode == 2) { // Fixed-size lacing
            int64_t rem = end - reader_->tell();
            if (rem < 0) return MediaError::InvalidData;
            size_t sz = static_cast<size_t>(rem) / num_frames;
            for (uint8_t i = 0; i < num_frames; ++i) frame_sizes[i] = sz;
        } else if (lacing_mode == 3) { // EBML lacing
            auto first_sz_res = Ebml::read_vint_size(*reader_);
            if (!first_sz_res.is_ok()) return first_sz_res.error();
            frame_sizes[0] = static_cast<size_t>(*first_sz_res);
            int64_t running_size = static_cast<int64_t>(frame_sizes[0]);

            for (uint8_t i = 1; i < num_frames - 1; ++i) {
                // Signed EBML diff
                auto diff_res = Ebml::read_vint_size(*reader_);
                if (!diff_res.is_ok()) return diff_res.error();
                int64_t diff = static_cast<int64_t>(*diff_res);
                running_size += diff;
                if (running_size < 0) return MediaError::InvalidData;
                frame_sizes[i] = static_cast<size_t>(running_size);
            }
            int64_t rem = end - reader_->tell();
            if (rem < 0) return MediaError::InvalidData;
            frame_sizes[num_frames - 1] = static_cast<size_t>(rem);
        }

        // Read frames
        for (uint8_t i = 0; i < num_frames; ++i) {
            std::vector<uint8_t> buf(frame_sizes[i]);
            auto r_res = reader_->read_exact(buf.data(), buf.size());
            if (!r_res.is_ok()) return r_res;

            Packet pkt = Packet::from_bytes(buf);
            pkt.set_pts(block_time_ms);
            pkt.set_dts(block_time_ms);
            pkt.set_stream_index(stream_idx);
            pkt.set_keyframe(is_keyframe && (i == 0));
            packet_queue_.push_back(std::move(pkt));
        }
    }

    reader_->seek(end, SeekOrigin::Begin);
    return MediaResult<void>::ok();
}

MediaResult<void> MkvDemuxer::parse_block_group(int64_t payload_size) {
    int64_t end = reader_->tell() + payload_size;
    while (reader_->tell() < end) {
        auto el = Ebml::read_element_header(*reader_);
        if (!el.is_ok()) break;

        if (el->id == ebml_id::Block) {
            auto res = parse_simple_block(static_cast<int64_t>(el->size));
            if (!res.is_ok()) return res;
        } else {
            reader_->skip(static_cast<int64_t>(el->size));
        }
    }
    reader_->seek(end, SeekOrigin::Begin);
    return MediaResult<void>::ok();
}

MediaResult<void> MkvDemuxer::seek(int64_t timestamp, int32_t /*stream_index*/) {
    packet_queue_.clear();

    if (!cues_.empty()) {
        // Binary search in Cues
        auto it = std::upper_bound(cues_.begin(), cues_.end(), timestamp,
            [](int64_t target, const MkvCue& cue) {
                return target < cue.time_ms;
            }
        );

        if (it != cues_.begin()) {
            --it;
        }

        int64_t target_offset = it->cluster_offset;
        auto s_res = reader_->seek(target_offset, SeekOrigin::Begin);
        if (!s_res.is_ok()) return s_res.error();

        return MediaResult<void>::ok();
    }

    if (first_cluster_offset_ > 0) {
        auto s_res = reader_->seek(first_cluster_offset_, SeekOrigin::Begin);
        if (!s_res.is_ok()) return s_res.error();
        if (timestamp <= 0) return MediaResult<void>::ok();

        // Sequential cluster scan
        while (reader_->remaining() >= 4) {
            int64_t cl_start = reader_->tell();
            auto el = Ebml::read_element_header(*reader_);
            if (!el.is_ok() || el->id != ebml_id::Cluster) break;

            int64_t cl_end = (el->size == EBML_UNKNOWN_SIZE) ? -1 : (el->data_offset + static_cast<int64_t>(el->size));
            int64_t cluster_time = -1;
            while ((cl_end < 0 || reader_->tell() < cl_end) && reader_->remaining() >= 2) {
                auto sub = Ebml::read_element_header(*reader_);
                if (!sub.is_ok()) break;
                if (sub->id == ebml_id::Timestamp) {
                    auto u = Ebml::read_uint(*reader_, static_cast<size_t>(sub->size));
                    if (u.is_ok()) cluster_time = static_cast<int64_t>(*u * timecode_scale_ns_ / 1000000);
                    break;
                } else {
                    reader_->skip(static_cast<int64_t>(sub->size));
                }
            }

            if (cluster_time >= timestamp) {
                reader_->seek(cl_start, SeekOrigin::Begin);
                return MediaResult<void>::ok();
            }

            if (cl_end > 0) {
                reader_->seek(cl_end, SeekOrigin::Begin);
            } else {
                break;
            }
        }
        return reader_->seek(first_cluster_offset_, SeekOrigin::Begin).error();
    }

    return MediaError::IoError;
}

} // namespace atum
