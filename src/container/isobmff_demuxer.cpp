#include "atum/container/isobmff_demuxer.hpp"
#include <algorithm>
#include <unordered_set>

namespace atum {

MediaResult<void> IsoBmffDemuxer::open(std::shared_ptr<IoReader> reader) {
    if (!reader) return MediaError::IoError;
    reader_ = std::move(reader);
    streams_.clear();
    tracks_.clear();
    track_id_to_idx_.clear();
    interleaved_samples_.clear();
    current_sample_idx_ = 0;
    is_fragmented_ = false;

    int64_t total_size = reader_->size();

    while (reader_->remaining() >= 8) {
        int64_t box_start = reader_->tell();
        auto sz_res = reader_->read_u32_be();
        auto type_res = reader_->read_u32_be();
        if (!sz_res.is_ok() || !type_res.is_ok()) break;

        uint64_t box_sz = *sz_res;
        uint32_t box_type = *type_res;
        size_t hdr_len = 8;

        if (box_sz == 1) { // 64-bit large size
            auto lsz = reader_->read_u64_be();
            if (!lsz.is_ok()) break;
            box_sz = *lsz;
            hdr_len = 16;
        } else if (box_sz == 0) { // Extends to EOF
            if (total_size > 0) {
                box_sz = static_cast<uint64_t>(total_size - box_start);
            }
        }

        if (box_type == make_fourcc('m', 'o', 'o', 'v')) {
            auto res = parse_moov(box_start + hdr_len, box_sz - hdr_len);
            if (!res.is_ok()) return res;
        } else if (box_type == make_fourcc('m', 'o', 'o', 'f')) {
            is_fragmented_ = true;
            auto res = parse_moof(box_start + hdr_len, box_sz - hdr_len);
            if (!res.is_ok()) return res;
        }

        // Seek to next top-level box
        if (box_sz > 0) {
            reader_->seek(box_start + box_sz, SeekOrigin::Begin);
        } else {
            break;
        }
    }

    if (tracks_.empty()) {
        return MediaError::InvalidData;
    }

    // Interleave all samples across all tracks ordered by byte offset
    for (size_t t = 0; t < tracks_.size(); ++t) {
        streams_.push_back(tracks_[t].stream);
        if (!is_fragmented_) {
            for (auto& s : tracks_[t].samples) {
                interleaved_samples_.push_back(s);
            }
        }
    }

    std::sort(interleaved_samples_.begin(), interleaved_samples_.end(),
        [](const IsoSampleRecord& a, const IsoSampleRecord& b) {
            return a.offset < b.offset;
        }
    );

    return MediaResult<void>::ok();
}

MediaResult<void> IsoBmffDemuxer::parse_moov(int64_t box_start, int64_t box_size) {
    int64_t end_offset = box_start + box_size;
    reader_->seek(box_start, SeekOrigin::Begin);

    while (reader_->tell() < end_offset && reader_->remaining() >= 8) {
        int64_t child_start = reader_->tell();
        auto sz_res = reader_->read_u32_be();
        auto type_res = reader_->read_u32_be();
        if (!sz_res.is_ok() || !type_res.is_ok()) break;

        uint64_t child_sz = *sz_res;
        uint32_t child_type = *type_res;
        size_t hdr_len = 8;
        if (child_sz == 1) {
            auto lsz = reader_->read_u64_be();
            if (!lsz.is_ok()) break;
            child_sz = *lsz;
            hdr_len = 16;
        }

        if (child_type == make_fourcc('m', 'v', 'h', 'd')) {
            auto ver_res = reader_->read_u8();
            if (ver_res.is_ok()) {
                uint8_t ver = *ver_res;
                reader_->skip(3); // flags
                if (ver == 1) {
                    reader_->skip(16); // creation + mod time
                    auto ts = reader_->read_u32_be();
                    auto dur = reader_->read_u64_be();
                    if (ts.is_ok() && *ts > 0 && dur.is_ok()) {
                        movie_timescale_ = *ts;
                        duration_ = static_cast<int64_t>(*dur * 1000000ULL / movie_timescale_);
                    }
                } else {
                    reader_->skip(8); // creation + mod time
                    auto ts = reader_->read_u32_be();
                    auto dur = reader_->read_u32_be();
                    if (ts.is_ok() && *ts > 0 && dur.is_ok()) {
                        movie_timescale_ = *ts;
                        duration_ = static_cast<int64_t>(static_cast<uint64_t>(*dur) * 1000000ULL / movie_timescale_);
                    }
                }
            }
        } else if (child_type == make_fourcc('t', 'r', 'a', 'k')) {
            parse_trak(child_start + hdr_len, child_sz - hdr_len);
        } else if (child_type == make_fourcc('m', 'v', 'e', 'x')) {
            parse_mvex(child_start + hdr_len, child_sz - hdr_len);
        }

        reader_->seek(child_start + child_sz, SeekOrigin::Begin);
    }
    return MediaResult<void>::ok();
}

MediaResult<void> IsoBmffDemuxer::parse_mvex(int64_t box_start, int64_t box_size) {
    int64_t end_offset = box_start + box_size;
    reader_->seek(box_start, SeekOrigin::Begin);

    while (reader_->tell() < end_offset && reader_->remaining() >= 8) {
        int64_t child_start = reader_->tell();
        auto sz_res = reader_->read_u32_be();
        auto type_res = reader_->read_u32_be();
        if (!sz_res.is_ok() || !type_res.is_ok()) break;

        uint64_t child_sz = *sz_res;
        uint32_t child_type = *type_res;

        if (child_type == make_fourcc('t', 'r', 'e', 'x')) {
            reader_->skip(4); // ver + flags
            auto trk_id = reader_->read_u32_be();
            auto def_desc_idx = reader_->read_u32_be();
            auto def_dur = reader_->read_u32_be();
            auto def_sz = reader_->read_u32_be();
            auto def_flags = reader_->read_u32_be();

            if (trk_id.is_ok() && track_id_to_idx_.contains(*trk_id)) {
                size_t idx = track_id_to_idx_[*trk_id];
                auto& trex = tracks_[idx].trex;
                trex.track_id = *trk_id;
                if (def_desc_idx.is_ok()) trex.default_sample_description_index = *def_desc_idx;
                if (def_dur.is_ok()) trex.default_sample_duration = *def_dur;
                if (def_sz.is_ok()) trex.default_sample_size = *def_sz;
                if (def_flags.is_ok()) trex.default_sample_flags = *def_flags;
            }
        }

        reader_->seek(child_start + child_sz, SeekOrigin::Begin);
    }
    return MediaResult<void>::ok();
}

MediaResult<void> IsoBmffDemuxer::parse_trak(int64_t box_start, int64_t box_size) {
    TrackContext ctx;
    ctx.stream.index = static_cast<int32_t>(tracks_.size());
    int64_t end_offset = box_start + box_size;
    reader_->seek(box_start, SeekOrigin::Begin);

    while (reader_->tell() < end_offset && reader_->remaining() >= 8) {
        int64_t child_start = reader_->tell();
        auto sz_res = reader_->read_u32_be();
        auto type_res = reader_->read_u32_be();
        if (!sz_res.is_ok() || !type_res.is_ok()) break;

        uint64_t child_sz = *sz_res;
        uint32_t child_type = *type_res;
        size_t hdr_len = 8;
        if (child_sz == 1) {
            auto lsz = reader_->read_u64_be();
            if (!lsz.is_ok()) break;
            child_sz = *lsz;
            hdr_len = 16;
        }

        if (child_type == make_fourcc('t', 'k', 'h', 'd')) {
            auto ver_res = reader_->read_u8();
            if (ver_res.is_ok()) {
                uint8_t ver = *ver_res;
                reader_->skip(3); // flags
                if (ver == 1) {
                    reader_->skip(16); // creation + mod time (16)
                    auto tid = reader_->read_u32_be();
                    if (tid.is_ok()) ctx.track_id = *tid;
                    reader_->skip(4); // reserved (4)
                    reader_->skip(8); // duration (8)
                } else {
                    reader_->skip(8);  // creation + mod time (8)
                    auto tid = reader_->read_u32_be();
                    if (tid.is_ok()) ctx.track_id = *tid;
                    reader_->skip(4); // reserved (4)
                    reader_->skip(4); // duration (4)
                }
                // Skip reserved(8) + layer(2) + alternate_group(2) + volume(2) + reserved(2) + matrix(36) = 52 bytes
                reader_->skip(52);
                auto w = reader_->read_u32_be();
                auto h = reader_->read_u32_be();
                if (w.is_ok() && h.is_ok()) {
                    ctx.stream.codec_params.width = *w >> 16;
                    ctx.stream.codec_params.height = *h >> 16;
                }
            }
        } else if (child_type == make_fourcc('m', 'd', 'i', 'a')) {
            parse_mdia(ctx, child_start + hdr_len, child_sz - hdr_len);
        }

        reader_->seek(child_start + child_sz, SeekOrigin::Begin);
    }

    if (ctx.track_id > 0) {
        track_id_to_idx_[ctx.track_id] = tracks_.size();
        tracks_.push_back(std::move(ctx));
    }
    return MediaResult<void>::ok();
}

MediaResult<void> IsoBmffDemuxer::parse_mdia(TrackContext& ctx, int64_t box_start, int64_t box_size) {
    int64_t end_offset = box_start + box_size;
    reader_->seek(box_start, SeekOrigin::Begin);

    while (reader_->tell() < end_offset && reader_->remaining() >= 8) {
        int64_t child_start = reader_->tell();
        auto sz_res = reader_->read_u32_be();
        auto type_res = reader_->read_u32_be();
        if (!sz_res.is_ok() || !type_res.is_ok()) break;

        uint64_t child_sz = *sz_res;
        uint32_t child_type = *type_res;
        size_t hdr_len = 8;
        if (child_sz == 1) {
            auto lsz = reader_->read_u64_be();
            if (!lsz.is_ok()) break;
            child_sz = *lsz;
            hdr_len = 16;
        }

        if (child_type == make_fourcc('m', 'd', 'h', 'd')) {
            auto ver_res = reader_->read_u8();
            if (ver_res.is_ok()) {
                uint8_t ver = *ver_res;
                reader_->skip(3);
                if (ver == 1) {
                    reader_->skip(16);
                    auto ts = reader_->read_u32_be();
                    auto dur = reader_->read_u64_be();
                    if (ts.is_ok() && *ts > 0 && dur.is_ok()) {
                        ctx.timescale = *ts;
                        ctx.stream.timebase = Rational{1, static_cast<int32_t>(ctx.timescale)};
                        ctx.stream.duration = static_cast<int64_t>(*dur);
                    }
                } else {
                    reader_->skip(8);
                    auto ts = reader_->read_u32_be();
                    auto dur = reader_->read_u32_be();
                    if (ts.is_ok() && *ts > 0 && dur.is_ok()) {
                        ctx.timescale = *ts;
                        ctx.stream.timebase = Rational{1, static_cast<int32_t>(ctx.timescale)};
                        ctx.stream.duration = static_cast<int64_t>(*dur);
                    }
                }
            }
        } else if (child_type == make_fourcc('h', 'd', 'l', 'r')) {
            reader_->skip(8); // ver + flags + pre_defined
            auto handler_type = reader_->read_u32_be();
            if (handler_type.is_ok()) {
                if (*handler_type == make_fourcc('v', 'i', 'd', 'e')) {
                    ctx.stream.codec_params.media_type = MediaType::Video;
                } else if (*handler_type == make_fourcc('s', 'o', 'u', 'n')) {
                    ctx.stream.codec_params.media_type = MediaType::Audio;
                }
            }
        } else if (child_type == make_fourcc('m', 'i', 'n', 'f')) {
            parse_minf(ctx, child_start + hdr_len, child_sz - hdr_len);
        }

        reader_->seek(child_start + child_sz, SeekOrigin::Begin);
    }
    return MediaResult<void>::ok();
}

MediaResult<void> IsoBmffDemuxer::parse_minf(TrackContext& ctx, int64_t box_start, int64_t box_size) {
    int64_t end_offset = box_start + box_size;
    reader_->seek(box_start, SeekOrigin::Begin);

    while (reader_->tell() < end_offset && reader_->remaining() >= 8) {
        int64_t child_start = reader_->tell();
        auto sz_res = reader_->read_u32_be();
        auto type_res = reader_->read_u32_be();
        if (!sz_res.is_ok() || !type_res.is_ok()) break;

        uint64_t child_sz = *sz_res;
        uint32_t child_type = *type_res;
        size_t hdr_len = 8;
        if (child_sz == 1) {
            auto lsz = reader_->read_u64_be();
            if (!lsz.is_ok()) break;
            child_sz = *lsz;
            hdr_len = 16;
        }

        if (child_type == make_fourcc('s', 't', 'b', 'l')) {
            parse_stbl(ctx, child_start + hdr_len, child_sz - hdr_len);
        }

        reader_->seek(child_start + child_sz, SeekOrigin::Begin);
    }
    return MediaResult<void>::ok();
}

MediaResult<void> IsoBmffDemuxer::parse_stbl(TrackContext& ctx, int64_t box_start, int64_t box_size) {
    int64_t end_offset = box_start + box_size;
    reader_->seek(box_start, SeekOrigin::Begin);

    struct SttsEntry { uint32_t count; uint32_t delta; };
    struct StscEntry { uint32_t first_chunk; uint32_t samples_per_chunk; uint32_t desc_idx; };
    struct CttsEntry { uint32_t count; int32_t offset; };

    std::vector<SttsEntry> stts_entries;
    std::vector<CttsEntry> ctts_entries;
    std::unordered_set<uint32_t> sync_samples;
    std::vector<StscEntry> stsc_entries;
    std::vector<uint32_t> sample_sizes;
    uint32_t uniform_sample_size = 0;
    std::vector<uint64_t> chunk_offsets;

    while (reader_->tell() < end_offset && reader_->remaining() >= 8) {
        int64_t child_start = reader_->tell();
        auto sz_res = reader_->read_u32_be();
        auto type_res = reader_->read_u32_be();
        if (!sz_res.is_ok() || !type_res.is_ok()) break;

        uint64_t child_sz = *sz_res;
        uint32_t child_type = *type_res;

        if (child_type == make_fourcc('s', 't', 's', 'd')) {
            reader_->skip(4); // ver + flags
            auto entry_cnt = reader_->read_u32_be();
            if (entry_cnt.is_ok() && *entry_cnt > 0) {
                for (uint32_t e = 0; e < *entry_cnt; ++e) {
                    if (reader_->tell() >= child_start + static_cast<int64_t>(child_sz)) break;
                    int64_t entry_start = reader_->tell();
                    auto esz_res = reader_->read_u32_be();
                    auto fmt_res = reader_->read_u32_be();
                    if (!esz_res.is_ok() || !fmt_res.is_ok()) break;

                    uint32_t entry_sz = *esz_res;
                    uint32_t f = *fmt_res;
                    int64_t entry_end = entry_start + entry_sz;

                    bool is_video = false;
                    bool is_audio = false;

                    if (f == make_fourcc('a', 'v', 'c', '1') || f == make_fourcc('a', 'v', 'c', '3')) {
                        ctx.stream.codec_params.codec_id = CodecId::H264;
                        is_video = true;
                    } else if (f == make_fourcc('h', 'v', 'c', '1') || f == make_fourcc('h', 'e', 'v', '1')) {
                        ctx.stream.codec_params.codec_id = CodecId::HEVC;
                        is_video = true;
                    } else if (f == make_fourcc('v', 'p', '0', '9')) {
                        ctx.stream.codec_params.codec_id = CodecId::VP9;
                        is_video = true;
                    } else if (f == make_fourcc('a', 'v', '0', '1')) {
                        ctx.stream.codec_params.codec_id = CodecId::AV1;
                        is_video = true;
                    } else if (f == make_fourcc('q', 'o', 'i', '0')) {
                        ctx.stream.codec_params.codec_id = CodecId::QOI_VIDEO;
                        is_video = true;
                    } else if (f == make_fourcc('m', 'p', '4', 'a')) {
                        ctx.stream.codec_params.codec_id = CodecId::AAC;
                        is_audio = true;
                    } else if (f == make_fourcc('O', 'p', 'u', 's')) {
                        ctx.stream.codec_params.codec_id = CodecId::Opus;
                        is_audio = true;
                    } else if (f == make_fourcc('f', 'L', 'a', 'C')) {
                        ctx.stream.codec_params.codec_id = CodecId::FLAC;
                        is_audio = true;
                    }

                    if (ctx.stream.codec_params.media_type == MediaType::Unknown) {
                        if (is_video) ctx.stream.codec_params.media_type = MediaType::Video;
                        else if (is_audio) ctx.stream.codec_params.media_type = MediaType::Audio;
                    }

                    if (ctx.stream.codec_params.media_type == MediaType::Video || is_video) {
                        // VisualSampleEntry fixed fields:
                        // 6 bytes reserved + 2 bytes data_reference_index = 8
                        // 2 (pre_defined) + 2 (reserved) + 12 (pre_defined) = 16
                        reader_->skip(24);
                        auto vw = reader_->read_u16_be();
                        auto vh = reader_->read_u16_be();
                        if (vw.is_ok() && vh.is_ok()) {
                            if (ctx.stream.codec_params.width == 0 && *vw > 0) ctx.stream.codec_params.width = *vw;
                            if (ctx.stream.codec_params.height == 0 && *vh > 0) ctx.stream.codec_params.height = *vh;
                        }
                        // 4 (horizres) + 4 (vertres) + 4 (reserved) + 2 (frame_count) + 32 (compressorname) + 2 (depth) + 2 (pre_defined) = 50
                        reader_->skip(50);

                        // Child boxes in VisualSampleEntry
                        while (reader_->tell() + 8 <= entry_end) {
                            int64_t sub_start = reader_->tell();
                            auto sub_sz = reader_->read_u32_be();
                            auto sub_type = reader_->read_u32_be();
                            if (!sub_sz.is_ok() || !sub_type.is_ok()) break;
                            if (*sub_sz < 8) break;

                            uint32_t payload_len = *sub_sz - 8;
                            if (*sub_type == make_fourcc('a', 'v', 'c', 'C') ||
                                *sub_type == make_fourcc('h', 'v', 'c', 'C') ||
                                *sub_type == make_fourcc('v', 'p', 'c', 'C') ||
                                *sub_type == make_fourcc('a', 'v', '1', 'C')) {
                                ctx.stream.codec_params.extradata.resize(payload_len);
                                auto read_res = reader_->read(ctx.stream.codec_params.extradata.data(), payload_len);
                                if (!read_res.is_ok()) {
                                    ctx.stream.codec_params.extradata.clear();
                                }
                            }
                            reader_->seek(sub_start + *sub_sz, SeekOrigin::Begin);
                        }
                    } else if (ctx.stream.codec_params.media_type == MediaType::Audio || is_audio) {
                        // AudioSampleEntry fixed fields:
                        // 6 bytes reserved + 2 bytes data_reference_index = 8
                        // 8 bytes reserved = 8
                        reader_->skip(16);
                        auto ch_cnt = reader_->read_u16_be();
                        auto s_size = reader_->read_u16_be();
                        reader_->skip(4); // 2 pre_defined + 2 reserved
                        auto s_rate = reader_->read_u32_be();
                        if (ch_cnt.is_ok()) ctx.stream.codec_params.channels = *ch_cnt;
                        if (s_size.is_ok()) {
                            if (*s_size == 16) ctx.stream.codec_params.audio_format = AudioSampleFormat::S16LE;
                            else if (*s_size == 32) ctx.stream.codec_params.audio_format = AudioSampleFormat::S32LE;
                            else if (*s_size == 8) ctx.stream.codec_params.audio_format = AudioSampleFormat::U8;
                        }
                        if (s_rate.is_ok()) ctx.stream.codec_params.sample_rate = *s_rate >> 16;

                        // Child boxes in AudioSampleEntry
                        while (reader_->tell() + 8 <= entry_end) {
                            int64_t sub_start = reader_->tell();
                            auto sub_sz = reader_->read_u32_be();
                            auto sub_type = reader_->read_u32_be();
                            if (!sub_sz.is_ok() || !sub_type.is_ok()) break;
                            if (*sub_sz < 8) break;

                            uint32_t payload_len = *sub_sz - 8;
                            if (*sub_type == make_fourcc('e', 's', 'd', 's') ||
                                *sub_type == make_fourcc('d', 'O', 'p', 's') ||
                                *sub_type == make_fourcc('d', 'f', 'L', 'a')) {
                                ctx.stream.codec_params.extradata.resize(payload_len);
                                auto read_res = reader_->read(ctx.stream.codec_params.extradata.data(), payload_len);
                                if (!read_res.is_ok()) {
                                    ctx.stream.codec_params.extradata.clear();
                                }
                            }
                            reader_->seek(sub_start + *sub_sz, SeekOrigin::Begin);
                        }
                    }
                    reader_->seek(entry_end, SeekOrigin::Begin);
                }
            }
        } else if (child_type == make_fourcc('s', 't', 't', 's')) {
            reader_->skip(4);
            auto count = reader_->read_u32_be();
            if (count.is_ok()) {
                stts_entries.reserve(*count);
                for (uint32_t i = 0; i < *count; ++i) {
                    auto c = reader_->read_u32_be();
                    auto d = reader_->read_u32_be();
                    if (!c.is_ok() || !d.is_ok()) break;
                    stts_entries.push_back({*c, *d});
                }
            }
        } else if (child_type == make_fourcc('c', 't', 't', 's')) {
            auto vf = reader_->read_u32_be();
            uint8_t ver = vf.is_ok() ? static_cast<uint8_t>(*vf >> 24) : 0;
            auto count = reader_->read_u32_be();
            if (count.is_ok()) {
                ctts_entries.reserve(*count);
                for (uint32_t i = 0; i < *count; ++i) {
                    auto c = reader_->read_u32_be();
                    auto off = reader_->read_u32_be();
                    if (!c.is_ok() || !off.is_ok()) break;
                    ctts_entries.push_back({*c, static_cast<int32_t>(*off)});
                }
            }
            (void)ver;
        } else if (child_type == make_fourcc('s', 't', 's', 's')) {
            reader_->skip(4);
            auto count = reader_->read_u32_be();
            if (count.is_ok()) {
                for (uint32_t i = 0; i < *count; ++i) {
                    auto s = reader_->read_u32_be();
                    if (s.is_ok()) sync_samples.insert(*s);
                }
            }
        } else if (child_type == make_fourcc('s', 't', 's', 'c')) {
            reader_->skip(4);
            auto count = reader_->read_u32_be();
            if (count.is_ok()) {
                stsc_entries.reserve(*count);
                for (uint32_t i = 0; i < *count; ++i) {
                    auto fc = reader_->read_u32_be();
                    auto spc = reader_->read_u32_be();
                    auto di = reader_->read_u32_be();
                    if (!fc.is_ok() || !spc.is_ok() || !di.is_ok()) break;
                    stsc_entries.push_back({*fc, *spc, *di});
                }
            }
        } else if (child_type == make_fourcc('s', 't', 's', 'z')) {
            reader_->skip(4);
            auto sz = reader_->read_u32_be();
            auto count = reader_->read_u32_be();
            if (sz.is_ok() && count.is_ok()) {
                uniform_sample_size = *sz;
                if (uniform_sample_size == 0) {
                    sample_sizes.reserve(*count);
                    for (uint32_t i = 0; i < *count; ++i) {
                        auto s = reader_->read_u32_be();
                        if (!s.is_ok()) break;
                        sample_sizes.push_back(*s);
                    }
                }
            }
        } else if (child_type == make_fourcc('s', 't', 'c', 'o')) {
            reader_->skip(4);
            auto count = reader_->read_u32_be();
            if (count.is_ok()) {
                chunk_offsets.reserve(*count);
                for (uint32_t i = 0; i < *count; ++i) {
                    auto off = reader_->read_u32_be();
                    if (!off.is_ok()) break;
                    chunk_offsets.push_back(*off);
                }
            }
        } else if (child_type == make_fourcc('c', 'o', '6', '4')) {
            reader_->skip(4);
            auto count = reader_->read_u32_be();
            if (count.is_ok()) {
                chunk_offsets.reserve(*count);
                for (uint32_t i = 0; i < *count; ++i) {
                    auto off = reader_->read_u64_be();
                    if (!off.is_ok()) break;
                    chunk_offsets.push_back(*off);
                }
            }
        }

        reader_->seek(child_start + child_sz, SeekOrigin::Begin);
    }

    // Reconstruct samples
    if (chunk_offsets.empty() || stsc_entries.empty()) return MediaResult<void>::ok();

    size_t sample_idx = 0;
    int64_t current_dts = 0;
    size_t stts_idx = 0;
    uint32_t stts_count_left = !stts_entries.empty() ? stts_entries[0].count : 0;

    size_t ctts_idx = 0;
    uint32_t ctts_count_left = !ctts_entries.empty() ? ctts_entries[0].count : 0;

    for (size_t chunk_idx = 0; chunk_idx < chunk_offsets.size(); ++chunk_idx) {
        uint32_t chunk_num = static_cast<uint32_t>(chunk_idx + 1);

        uint32_t samples_per_chunk = 1;
        for (const auto& entry : stsc_entries) {
            if (chunk_num >= entry.first_chunk) {
                samples_per_chunk = entry.samples_per_chunk;
            } else {
                break;
            }
        }

        uint64_t cur_offset = chunk_offsets[chunk_idx];

        for (uint32_t s = 0; s < samples_per_chunk; ++s) {
            uint32_t s_sz = uniform_sample_size;
            if (s_sz == 0 && sample_idx < sample_sizes.size()) {
                s_sz = sample_sizes[sample_idx];
            }

            uint32_t dur = 1;
            if (stts_idx < stts_entries.size()) {
                dur = stts_entries[stts_idx].delta;
                stts_count_left--;
                if (stts_count_left == 0) {
                    stts_idx++;
                    if (stts_idx < stts_entries.size()) {
                        stts_count_left = stts_entries[stts_idx].count;
                    }
                }
            }

            int32_t ctts_offset = 0;
            if (ctts_idx < ctts_entries.size()) {
                ctts_offset = ctts_entries[ctts_idx].offset;
                ctts_count_left--;
                if (ctts_count_left == 0) {
                    ctts_idx++;
                    if (ctts_idx < ctts_entries.size()) {
                        ctts_count_left = ctts_entries[ctts_idx].count;
                    }
                }
            }

            bool is_key = sync_samples.empty() || sync_samples.count(static_cast<uint32_t>(sample_idx + 1)) > 0;

            IsoSampleRecord rec;
            rec.offset = cur_offset;
            rec.size = s_sz;
            rec.dts = current_dts;
            rec.pts = current_dts + ctts_offset;
            rec.duration = dur;
            rec.is_keyframe = is_key;
            rec.stream_index = ctx.stream.index;

            ctx.samples.push_back(rec);

            cur_offset += s_sz;
            current_dts += dur;
            sample_idx++;
        }
    }

    ctx.last_dts = current_dts;
    return MediaResult<void>::ok();
}

MediaResult<void> IsoBmffDemuxer::parse_moof(int64_t box_start, int64_t box_size) {
    int64_t end_offset = box_start + box_size;
    reader_->seek(box_start, SeekOrigin::Begin);

    while (reader_->tell() < end_offset && reader_->remaining() >= 8) {
        int64_t child_start = reader_->tell();
        auto sz_res = reader_->read_u32_be();
        auto type_res = reader_->read_u32_be();
        if (!sz_res.is_ok() || !type_res.is_ok()) break;

        uint64_t child_sz = *sz_res;
        uint32_t child_type = *type_res;
        size_t hdr_len = 8;
        if (child_sz == 1) {
            auto lsz = reader_->read_u64_be();
            if (!lsz.is_ok()) break;
            child_sz = *lsz;
            hdr_len = 16;
        }

        if (child_type == make_fourcc('t', 'r', 'a', 'f')) {
            parse_traf(child_start + hdr_len, child_sz - hdr_len, box_start - 8);
        }

        reader_->seek(child_start + child_sz, SeekOrigin::Begin);
    }

    return MediaResult<void>::ok();
}

MediaResult<void> IsoBmffDemuxer::parse_traf(int64_t box_start, int64_t box_size, int64_t moof_start) {
    int64_t end_offset = box_start + box_size;
    reader_->seek(box_start, SeekOrigin::Begin);

    uint32_t trk_id = 0;
    uint64_t base_data_offset = static_cast<uint64_t>(moof_start);
    uint32_t def_dur = 0;
    uint32_t def_sz = 0;
    uint32_t def_flags = 0;
    int64_t base_decode_time = -1;

    TrackContext* p_ctx = nullptr;

    while (reader_->tell() < end_offset && reader_->remaining() >= 8) {
        int64_t child_start = reader_->tell();
        auto sz_res = reader_->read_u32_be();
        auto type_res = reader_->read_u32_be();
        if (!sz_res.is_ok() || !type_res.is_ok()) break;

        uint64_t child_sz = *sz_res;
        uint32_t child_type = *type_res;

        if (child_type == make_fourcc('t', 'f', 'h', 'd')) {
            auto vf = reader_->read_u32_be();
            uint32_t flags = vf.is_ok() ? (*vf & 0x00FFFFFF) : 0;
            auto tid = reader_->read_u32_be();
            if (tid.is_ok()) {
                trk_id = *tid;
                if (track_id_to_idx_.contains(trk_id)) {
                    p_ctx = &tracks_[track_id_to_idx_[trk_id]];
                    def_dur = p_ctx->trex.default_sample_duration;
                    def_sz = p_ctx->trex.default_sample_size;
                    def_flags = p_ctx->trex.default_sample_flags;
                }
            }

            if ((flags & 0x01) != 0) { // base-data-offset-present
                auto bdo = reader_->read_u64_be();
                if (bdo.is_ok()) base_data_offset = *bdo;
            }
            if ((flags & 0x02) != 0) { // sample-description-index-present
                reader_->skip(4);
            }
            if ((flags & 0x08) != 0) { // default-sample-duration-present
                auto dd = reader_->read_u32_be();
                if (dd.is_ok()) def_dur = *dd;
            }
            if ((flags & 0x10) != 0) { // default-sample-size-present
                auto ds = reader_->read_u32_be();
                if (ds.is_ok()) def_sz = *ds;
            }
            if ((flags & 0x20) != 0) { // default-sample-flags-present
                auto df = reader_->read_u32_be();
                if (df.is_ok()) def_flags = *df;
            }
        } else if (child_type == make_fourcc('t', 'f', 'd', 't')) {
            auto vf = reader_->read_u32_be();
            uint8_t ver = vf.is_ok() ? static_cast<uint8_t>(*vf >> 24) : 0;
            if (ver == 1) {
                auto bdt = reader_->read_u64_be();
                if (bdt.is_ok()) base_decode_time = static_cast<int64_t>(*bdt);
            } else {
                auto bdt = reader_->read_u32_be();
                if (bdt.is_ok()) base_decode_time = static_cast<int64_t>(*bdt);
            }
        } else if (child_type == make_fourcc('t', 'r', 'u', 'n')) {
            if (!p_ctx) {
                reader_->seek(child_start + child_sz, SeekOrigin::Begin);
                continue;
            }

            auto vf = reader_->read_u32_be();
            uint32_t flags = vf.is_ok() ? (*vf & 0x00FFFFFF) : 0;
            auto scnt = reader_->read_u32_be();
            if (!scnt.is_ok()) break;

            uint32_t sample_count = *scnt;
            int64_t data_offset = 0;
            if ((flags & 0x01) != 0) { // data-offset-present
                auto doff = reader_->read_u32_be();
                if (doff.is_ok()) data_offset = static_cast<int32_t>(*doff);
            }

            uint32_t first_sample_flags = def_flags;
            if ((flags & 0x04) != 0) { // first-sample-flags-present
                auto fsf = reader_->read_u32_be();
                if (fsf.is_ok()) first_sample_flags = *fsf;
            }

            uint64_t cur_sample_offset = base_data_offset + data_offset;
            int64_t cur_dts = (base_decode_time >= 0) ? base_decode_time : p_ctx->last_dts;

            for (uint32_t i = 0; i < sample_count; ++i) {
                uint32_t dur = def_dur;
                uint32_t sz = def_sz;
                uint32_t sflags = (i == 0) ? first_sample_flags : def_flags;
                int32_t comp_off = 0;

                if ((flags & 0x100) != 0) { // sample-duration-present
                    auto d = reader_->read_u32_be();
                    if (d.is_ok()) dur = *d;
                }
                if ((flags & 0x200) != 0) { // sample-size-present
                    auto s = reader_->read_u32_be();
                    if (s.is_ok()) sz = *s;
                }
                if ((flags & 0x400) != 0) { // sample-flags-present
                    auto f = reader_->read_u32_be();
                    if (f.is_ok()) sflags = *f;
                }
                if ((flags & 0x800) != 0) { // sample-composition-time-offsets-present
                    auto c = reader_->read_u32_be();
                    if (c.is_ok()) comp_off = static_cast<int32_t>(*c);
                }

                bool is_key = ((sflags & 0x00010000) == 0) && ((sflags & 0x02000000) == 0);

                IsoSampleRecord rec;
                rec.offset = cur_sample_offset;
                rec.size = sz;
                rec.dts = cur_dts;
                rec.pts = cur_dts + comp_off;
                rec.duration = dur;
                rec.is_keyframe = is_key;
                rec.stream_index = p_ctx->stream.index;

                interleaved_samples_.push_back(rec);

                cur_sample_offset += sz;
                cur_dts += dur;
            }

            p_ctx->last_dts = cur_dts;
        }

        reader_->seek(child_start + child_sz, SeekOrigin::Begin);
    }
    return MediaResult<void>::ok();
}

MediaResult<void> IsoBmffDemuxer::read_next_fragment() {
    int64_t total_size = reader_->size();
    while (reader_->remaining() >= 8) {
        int64_t box_start = reader_->tell();
        auto sz_res = reader_->read_u32_be();
        auto type_res = reader_->read_u32_be();
        if (!sz_res.is_ok() || !type_res.is_ok()) break;

        uint64_t box_sz = *sz_res;
        uint32_t box_type = *type_res;
        size_t hdr_len = 8;
        if (box_sz == 1) {
            auto lsz = reader_->read_u64_be();
            if (!lsz.is_ok()) break;
            box_sz = *lsz;
            hdr_len = 16;
        } else if (box_sz == 0 && total_size > 0) {
            box_sz = static_cast<uint64_t>(total_size - box_start);
        }

        if (box_type == make_fourcc('m', 'o', 'o', 'f')) {
            size_t prev_count = interleaved_samples_.size();
            auto res = parse_moof(box_start + hdr_len, box_sz - hdr_len);
            if (!res.is_ok()) return res;
            if (interleaved_samples_.size() > prev_count) {
                return MediaResult<void>::ok();
            }
        }

        if (box_sz > 0) {
            reader_->seek(box_start + box_sz, SeekOrigin::Begin);
        } else {
            break;
        }
    }
    return MediaError::EndOfStream;
}

MediaResult<void> IsoBmffDemuxer::read_packet(Packet& packet) {
    if (current_sample_idx_ >= interleaved_samples_.size()) {
        if (is_fragmented_) {
            auto res = read_next_fragment();
            if (!res.is_ok()) return res;
        } else {
            return MediaError::EndOfStream;
        }
    }

    if (current_sample_idx_ >= interleaved_samples_.size()) {
        return MediaError::EndOfStream;
    }

    const auto& rec = interleaved_samples_[current_sample_idx_++];
    auto s_res = reader_->seek(rec.offset, SeekOrigin::Begin);
    if (!s_res.is_ok()) return s_res.error();

    std::vector<uint8_t> buf(rec.size);
    auto r_res = reader_->read_exact(buf.data(), rec.size);
    if (!r_res.is_ok()) return r_res;

    packet = Packet::from_bytes(buf);
    packet.set_pts(rec.pts);
    packet.set_dts(rec.dts);
    packet.set_stream_index(rec.stream_index);
    packet.set_keyframe(rec.is_keyframe);

    return MediaResult<void>::ok();
}

MediaResult<void> IsoBmffDemuxer::seek(int64_t timestamp, int32_t stream_index) {
    if (interleaved_samples_.empty()) return MediaError::IoError;

    // Find sample nearest to timestamp
    size_t best_idx = 0;
    int64_t best_diff = 0x7FFFFFFFFFFFFFFFLL;

    for (size_t i = 0; i < interleaved_samples_.size(); ++i) {
        const auto& s = interleaved_samples_[i];
        if (stream_index >= 0 && s.stream_index != stream_index) continue;
        int64_t diff = std::abs(s.pts - timestamp);
        if (diff < best_diff) {
            best_diff = diff;
            best_idx = i;
        }
    }

    // Rewind to preceding keyframe
    while (best_idx > 0 && !interleaved_samples_[best_idx].is_keyframe) {
        best_idx--;
    }

    current_sample_idx_ = best_idx;
    return MediaResult<void>::ok();
}

} // namespace atum
