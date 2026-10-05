#include "atum/container/isobmff_muxer.hpp"
#include <algorithm>
#include <cstring>

namespace atum {

uint32_t IsoBmffMuxer::map_codec_to_fourcc(CodecId id) noexcept {
    switch (id) {
        case CodecId::H264:      return make_fourcc('a', 'v', 'c', '1');
        case CodecId::HEVC:      return make_fourcc('h', 'v', 'c', '1');
        case CodecId::VP9:       return make_fourcc('v', 'p', '0', '9');
        case CodecId::AV1:       return make_fourcc('a', 'v', '0', '1');
        case CodecId::AAC:       return make_fourcc('m', 'p', '4', 'a');
        case CodecId::Opus:      return make_fourcc('O', 'p', 'u', 's');
        case CodecId::FLAC:      return make_fourcc('f', 'L', 'a', 'C');
        case CodecId::QOI_VIDEO: return make_fourcc('q', 'o', 'i', '0');
        default:                 return make_fourcc('m', 'p', '4', 'v');
    }
}

MediaResult<void> IsoBmffMuxer::open(std::shared_ptr<IoWriter> writer) {
    if (!writer) return MediaError::IoError;
    writer_ = std::move(writer);
    tracks_.clear();
    mdat_header_offset_ = 0;
    mdat_data_start_ = 0;
    return MediaResult<void>::ok();
}

MediaResult<int32_t> IsoBmffMuxer::add_stream(const CodecParameters& params, Rational timebase) {
    int32_t idx = static_cast<int32_t>(tracks_.size());
    TrackInfo ti;
    ti.stream_index = idx;
    ti.track_id = static_cast<uint32_t>(idx + 1);
    ti.params = params;
    ti.timebase = timebase;
    ti.timescale = (timebase.den > 0 && timebase.num > 0) ? (timebase.den / timebase.num) : 1000;
    tracks_.push_back(std::move(ti));
    return idx;
}

namespace {
    class BoxWriter {
    public:
        void write_u8(uint8_t val) { buf_.push_back(val); }
        void write_u16_be(uint16_t val) {
            buf_.push_back(static_cast<uint8_t>(val >> 8));
            buf_.push_back(static_cast<uint8_t>(val & 0xFF));
        }
        void write_u32_be(uint32_t val) {
            buf_.push_back(static_cast<uint8_t>(val >> 24));
            buf_.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
            buf_.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
            buf_.push_back(static_cast<uint8_t>(val & 0xFF));
        }
        void write_u64_be(uint64_t val) {
            write_u32_be(static_cast<uint32_t>(val >> 32));
            write_u32_be(static_cast<uint32_t>(val & 0xFFFFFFFF));
        }
        void write_bytes(const uint8_t* data, size_t size) {
            buf_.insert(buf_.end(), data, data + size);
        }
        void write_fourcc(uint32_t f) {
            write_u32_be(f);
        }

        void write_box(uint32_t type, const BoxWriter& child) {
            uint32_t sz = static_cast<uint32_t>(child.size() + 8);
            write_u32_be(sz);
            write_fourcc(type);
            write_bytes(child.data(), child.size());
        }

        [[nodiscard]] size_t size() const noexcept { return buf_.size(); }
        [[nodiscard]] const uint8_t* data() const noexcept { return buf_.data(); }
    private:
        std::vector<uint8_t> buf_;
    };
} // anonymous namespace

MediaResult<void> IsoBmffMuxer::write_header() {
    if (!writer_) return MediaError::IoError;

    // 1. Write ftyp box
    BoxWriter ftyp;
    ftyp.write_fourcc(make_fourcc('i', 's', 'o', 'm')); // major brand
    ftyp.write_u32_be(0x00000200);                      // minor version
    ftyp.write_fourcc(make_fourcc('i', 's', 'o', 'm')); // compatible brands
    ftyp.write_fourcc(make_fourcc('i', 's', 'o', '2'));
    ftyp.write_fourcc(make_fourcc('a', 'v', 'c', '1'));
    ftyp.write_fourcc(make_fourcc('m', 'p', '4', '1'));

    BoxWriter root;
    root.write_box(make_fourcc('f', 't', 'y', 'p'), ftyp);
    auto res = writer_->write_exact(root.data(), root.size());
    if (!res.is_ok()) return res;

    // 2. Write placeholder 64-bit mdat header
    mdat_header_offset_ = writer_->tell();
    uint8_t mdat_hdr[16];
    mdat_hdr[0] = 0x00; mdat_hdr[1] = 0x00; mdat_hdr[2] = 0x00; mdat_hdr[3] = 0x01; // size = 1 (64-bit size follows)
    uint32_t mdat_type = make_fourcc('m', 'd', 'a', 't');
    mdat_hdr[4] = static_cast<uint8_t>(mdat_type >> 24);
    mdat_hdr[5] = static_cast<uint8_t>((mdat_type >> 16) & 0xFF);
    mdat_hdr[6] = static_cast<uint8_t>((mdat_type >> 8) & 0xFF);
    mdat_hdr[7] = static_cast<uint8_t>(mdat_type & 0xFF);
    std::memset(mdat_hdr + 8, 0, 8); // Placeholder size 0

    res = writer_->write_exact(mdat_hdr, 16);
    if (!res.is_ok()) return res;

    mdat_data_start_ = writer_->tell();
    return MediaResult<void>::ok();
}

MediaResult<void> IsoBmffMuxer::write_packet(const Packet& packet) {
    if (!writer_) return MediaError::IoError;
    if (packet.stream_index() < 0 || static_cast<size_t>(packet.stream_index()) >= tracks_.size()) {
        return MediaError::InvalidData;
    }

    auto& trk = tracks_[packet.stream_index()];

    int64_t pts = packet.pts() >= 0 ? packet.pts() : trk.last_dts;
    int64_t dts = packet.dts() >= 0 ? packet.dts() : pts;

    int64_t dur = 1;
    if (!trk.samples.empty()) {
        int64_t prev_dur = dts - trk.samples.back().dts;
        if (prev_dur > 0) {
            trk.samples.back().duration = prev_dur;
        }
    }

    IsoSampleRecord rec;
    rec.offset = static_cast<uint64_t>(writer_->tell());
    rec.size = static_cast<uint32_t>(packet.size());
    rec.pts = pts;
    rec.dts = dts;
    rec.duration = dur;
    rec.is_keyframe = packet.is_keyframe();
    rec.stream_index = trk.stream_index;

    trk.samples.push_back(rec);
    trk.last_dts = dts;

    return writer_->write_exact(packet.data(), packet.size());
}

MediaResult<void> IsoBmffMuxer::write_trailer() {
    if (!writer_) return MediaError::IoError;

    int64_t mdat_end = writer_->tell();
    uint64_t total_mdat_box_size = static_cast<uint64_t>(mdat_end - mdat_header_offset_);

    // Update 64-bit size in mdat header
    auto s_res = writer_->seek(mdat_header_offset_ + 8, SeekOrigin::Begin);
    if (!s_res.is_ok()) return s_res.error();

    auto w_res = writer_->write_u64_be(total_mdat_box_size);
    if (!w_res.is_ok()) return w_res;

    // Return to end of mdat to append moov box
    s_res = writer_->seek(mdat_end, SeekOrigin::Begin);
    if (!s_res.is_ok()) return s_res.error();

    // Calculate maximum movie duration in movie_timescale_
    int64_t max_movie_dur = 0;
    for (const auto& trk : tracks_) {
        if (!trk.samples.empty()) {
            int64_t trk_dur_ms = rescale_timestamp(trk.samples.back().dts + trk.samples.back().duration,
                                                   trk.timebase, Rational{1, static_cast<int32_t>(movie_timescale_)});
            max_movie_dur = std::max(max_movie_dur, trk_dur_ms);
        }
    }

    // Build moov hierarchy
    BoxWriter moov;

    // 1. mvhd
    BoxWriter mvhd;
    mvhd.write_u32_be(0); // version 0 + flags 0
    mvhd.write_u32_be(0); // creation_time
    mvhd.write_u32_be(0); // modification_time
    mvhd.write_u32_be(movie_timescale_);
    mvhd.write_u32_be(static_cast<uint32_t>(max_movie_dur));
    mvhd.write_u32_be(0x00010000); // rate 1.0
    mvhd.write_u16_be(0x0100);     // volume 1.0
    mvhd.write_u16_be(0);          // reserved
    mvhd.write_u32_be(0);          // reserved
    mvhd.write_u32_be(0);
    // 36-byte unity matrix
    uint32_t matrix[9] = {0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000};
    for (uint32_t m : matrix) mvhd.write_u32_be(m);
    // pre_defined 24 bytes
    for (int i = 0; i < 6; ++i) mvhd.write_u32_be(0);
    mvhd.write_u32_be(static_cast<uint32_t>(tracks_.size() + 1)); // next_track_id

    moov.write_box(make_fourcc('m', 'v', 'h', 'd'), mvhd);

    // 2. trak for each track
    for (const auto& trk : tracks_) {
        BoxWriter trak;

        // tkhd
        BoxWriter tkhd;
        tkhd.write_u32_be(0x00000003); // version 0, flags enabled + in_movie
        tkhd.write_u32_be(0); // creation
        tkhd.write_u32_be(0); // mod
        tkhd.write_u32_be(trk.track_id);
        tkhd.write_u32_be(0); // reserved
        int64_t trk_dur = !trk.samples.empty() ? (trk.samples.back().dts + trk.samples.back().duration) : 0;
        tkhd.write_u32_be(static_cast<uint32_t>(trk_dur));
        tkhd.write_u32_be(0); // reserved
        tkhd.write_u32_be(0);
        tkhd.write_u16_be(0); // layer
        tkhd.write_u16_be(0); // alt group
        tkhd.write_u16_be(trk.params.media_type == MediaType::Audio ? 0x0100 : 0); // volume
        tkhd.write_u16_be(0);
        for (uint32_t m : matrix) tkhd.write_u32_be(m);
        uint32_t w = trk.params.width > 0 ? trk.params.width : 1920;
        uint32_t h = trk.params.height > 0 ? trk.params.height : 1080;
        tkhd.write_u32_be(w << 16);
        tkhd.write_u32_be(h << 16);

        trak.write_box(make_fourcc('t', 'k', 'h', 'd'), tkhd);

        // mdia
        BoxWriter mdia;

        // mdhd
        BoxWriter mdhd;
        mdhd.write_u32_be(0);
        mdhd.write_u32_be(0);
        mdhd.write_u32_be(0);
        mdhd.write_u32_be(trk.timescale);
        mdhd.write_u32_be(static_cast<uint32_t>(trk_dur));
        mdhd.write_u16_be(0x55C4); // language 'und'
        mdhd.write_u16_be(0);
        mdia.write_box(make_fourcc('m', 'd', 'h', 'd'), mdhd);

        // hdlr
        BoxWriter hdlr;
        hdlr.write_u32_be(0);
        hdlr.write_u32_be(0);
        uint32_t htype = (trk.params.media_type == MediaType::Video) ? make_fourcc('v', 'i', 'd', 'e') : make_fourcc('s', 'o', 'u', 'n');
        hdlr.write_fourcc(htype);
        for (int i = 0; i < 3; ++i) hdlr.write_u32_be(0);
        hdlr.write_u8(0); // null name string
        mdia.write_box(make_fourcc('h', 'd', 'l', 'r'), hdlr);

        // minf
        BoxWriter minf;
        if (trk.params.media_type == MediaType::Video) {
            BoxWriter vmhd;
            vmhd.write_u32_be(1); // flags = 1
            vmhd.write_u16_be(0); // graphics_mode = copy
            vmhd.write_u16_be(0); // opcolor
            vmhd.write_u16_be(0);
            vmhd.write_u16_be(0);
            minf.write_box(make_fourcc('v', 'm', 'h', 'd'), vmhd);
        } else {
            BoxWriter smhd;
            smhd.write_u32_be(0);
            smhd.write_u16_be(0); // balance
            smhd.write_u16_be(0);
            minf.write_box(make_fourcc('s', 'm', 'h', 'd'), smhd);
        }

        // dinf -> dref -> url
        BoxWriter url;
        url.write_u32_be(1); // flags = 1 (in same file)
        BoxWriter dref;
        dref.write_u32_be(0);
        dref.write_u32_be(1); // 1 entry
        dref.write_box(make_fourcc('u', 'r', 'l', ' '), url);
        BoxWriter dinf;
        dinf.write_box(make_fourcc('d', 'r', 'e', 'f'), dref);
        minf.write_box(make_fourcc('d', 'i', 'n', 'f'), dinf);

        // stbl
        BoxWriter stbl;

        // stsd
        BoxWriter stsd;
        stsd.write_u32_be(0);
        stsd.write_u32_be(1); // 1 entry

        BoxWriter entry;
        entry.write_u32_be(0); // reserved
        entry.write_u16_be(0);
        entry.write_u16_be(1); // data_reference_index = 1
        if (trk.params.media_type == MediaType::Video) {
            entry.write_u16_be(0); // pre_defined
            entry.write_u16_be(0); // reserved
            for (int i = 0; i < 3; ++i) entry.write_u32_be(0);
            entry.write_u16_be(static_cast<uint16_t>(w));
            entry.write_u16_be(static_cast<uint16_t>(h));
            entry.write_u32_be(0x00480000); // 72 dpi
            entry.write_u32_be(0x00480000);
            entry.write_u32_be(0); // reserved
            entry.write_u16_be(1); // frame_count
            for (int i = 0; i < 8; ++i) entry.write_u32_be(0); // compressor name 32 bytes
            entry.write_u16_be(0x0018); // depth 24-bit
            entry.write_u16_be(0xFFFF); // pre_defined -1

            if (!trk.params.extradata.empty()) {
                BoxWriter avcC;
                avcC.write_bytes(trk.params.extradata.data(), trk.params.extradata.size());
                entry.write_box(make_fourcc('a', 'v', 'c', 'C'), avcC);
            }
        } else {
            entry.write_u16_be(0); // version
            entry.write_u16_be(0); // revision
            entry.write_u32_be(0); // vendor
            entry.write_u16_be(static_cast<uint16_t>(trk.params.channels > 0 ? trk.params.channels : 2));
            entry.write_u16_be(16); // sample_size
            entry.write_u16_be(0); // packet_size
            entry.write_u16_be(0); // compression_id
            entry.write_u32_be(static_cast<uint32_t>((trk.params.sample_rate > 0 ? trk.params.sample_rate : 44100) << 16));
        }

        uint32_t codec_fourcc = map_codec_to_fourcc(trk.params.codec_id);
        stsd.write_box(codec_fourcc, entry);
        stbl.write_box(make_fourcc('s', 't', 's', 'd'), stsd);

        // stts (decoding time-to-sample)
        BoxWriter stts;
        stts.write_u32_be(0);
        // Run-length delta compression
        struct DeltaRun { uint32_t count; uint32_t delta; };
        std::vector<DeltaRun> stts_runs;
        for (const auto& s : trk.samples) {
            uint32_t d = static_cast<uint32_t>(s.duration > 0 ? s.duration : 1);
            if (!stts_runs.empty() && stts_runs.back().delta == d) {
                stts_runs.back().count++;
            } else {
                stts_runs.push_back({1, d});
            }
        }
        stts.write_u32_be(static_cast<uint32_t>(stts_runs.size()));
        for (const auto& r : stts_runs) {
            stts.write_u32_be(r.count);
            stts.write_u32_be(r.delta);
        }
        stbl.write_box(make_fourcc('s', 't', 't', 's'), stts);

        // ctts (composition time offsets if any PTS != DTS)
        bool has_ctts = false;
        for (const auto& s : trk.samples) {
            if (s.pts != s.dts) {
                has_ctts = true;
                break;
            }
        }
        if (has_ctts) {
            BoxWriter ctts;
            ctts.write_u32_be(0); // version 0
            struct CttsRun { uint32_t count; int32_t offset; };
            std::vector<CttsRun> ctts_runs;
            for (const auto& s : trk.samples) {
                int32_t off = static_cast<int32_t>(s.pts - s.dts);
                if (!ctts_runs.empty() && ctts_runs.back().offset == off) {
                    ctts_runs.back().count++;
                } else {
                    ctts_runs.push_back({1, off});
                }
            }
            ctts.write_u32_be(static_cast<uint32_t>(ctts_runs.size()));
            for (const auto& r : ctts_runs) {
                ctts.write_u32_be(r.count);
                ctts.write_u32_be(static_cast<uint32_t>(r.offset));
            }
            stbl.write_box(make_fourcc('c', 't', 't', 's'), ctts);
        }

        // stss (sync samples / keyframes)
        std::vector<uint32_t> key_indices;
        for (size_t i = 0; i < trk.samples.size(); ++i) {
            if (trk.samples[i].is_keyframe) {
                key_indices.push_back(static_cast<uint32_t>(i + 1));
            }
        }
        if (!key_indices.empty()) {
            BoxWriter stss;
            stss.write_u32_be(0);
            stss.write_u32_be(static_cast<uint32_t>(key_indices.size()));
            for (uint32_t k : key_indices) stss.write_u32_be(k);
            stbl.write_box(make_fourcc('s', 't', 's', 's'), stss);
        }

        // stsc (sample to chunk - 1 sample per chunk)
        BoxWriter stsc;
        stsc.write_u32_be(0);
        stsc.write_u32_be(1); // 1 table entry
        stsc.write_u32_be(1); // first_chunk
        stsc.write_u32_be(1); // samples_per_chunk
        stsc.write_u32_be(1); // sample_description_index
        stbl.write_box(make_fourcc('s', 't', 's', 'c'), stsc);

        // stsz (sample sizes)
        BoxWriter stsz;
        stsz.write_u32_be(0);
        stsz.write_u32_be(0); // variable size
        stsz.write_u32_be(static_cast<uint32_t>(trk.samples.size()));
        for (const auto& s : trk.samples) stsz.write_u32_be(s.size);
        stbl.write_box(make_fourcc('s', 't', 's', 'z'), stsz);

        // co64 (64-bit chunk offsets)
        BoxWriter co64;
        co64.write_u32_be(0);
        co64.write_u32_be(static_cast<uint32_t>(trk.samples.size()));
        for (const auto& s : trk.samples) co64.write_u64_be(s.offset);
        stbl.write_box(make_fourcc('c', 'o', '6', '4'), co64);

        minf.write_box(make_fourcc('s', 't', 'b', 'l'), stbl);
        mdia.write_box(make_fourcc('m', 'i', 'n', 'f'), minf);
        trak.write_box(make_fourcc('m', 'd', 'i', 'a'), mdia);
        moov.write_box(make_fourcc('t', 'r', 'a', 'k'), trak);
    }

    BoxWriter root_moov;
    root_moov.write_box(make_fourcc('m', 'o', 'o', 'v'), moov);

    auto res = writer_->write_exact(root_moov.data(), root_moov.size());
    if (!res.is_ok()) return res;

    return writer_->flush();
}

} // namespace atum
