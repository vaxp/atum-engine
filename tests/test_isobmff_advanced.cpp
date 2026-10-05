#include "atum/container/isobmff_demuxer.hpp"
#include "atum/container/isobmff_muxer.hpp"
#include "atum/io/memory_stream.hpp"
#include <cassert>
#include <iostream>
#include <vector>

using namespace atum;

void test_isobmff_muxer_and_ctts() {
    std::cout << "[TEST] Running test_isobmff_muxer_and_ctts..." << std::endl;

    auto writer = std::make_shared<MemoryWriter>();
    IsoBmffMuxer muxer;
    auto res = muxer.open(writer);
    assert(res.is_ok());

    CodecParameters vparams;
    vparams.media_type = MediaType::Video;
    vparams.codec_id = CodecId::H264;
    vparams.width = 1920;
    vparams.height = 1080;
    int32_t v_idx = *muxer.add_stream(vparams, Rational{1, 1000});
    assert(v_idx == 0);

    res = muxer.write_header();
    assert(res.is_ok());

    // Write packets with composition offsets (B-frames: PTS != DTS)
    // Frame 0 (I-frame): DTS=0, PTS=0
    // Frame 1 (P-frame): DTS=33, PTS=100
    // Frame 2 (B-frame): DTS=66, PTS=33
    // Frame 3 (B-frame): DTS=100, PTS=66
    int64_t dts_arr[] = {0, 33, 66, 100};
    int64_t pts_arr[] = {0, 100, 33, 66};

    for (int i = 0; i < 4; ++i) {
        std::vector<uint8_t> vdata(256, static_cast<uint8_t>(i + 1));
        Packet pkt = Packet::from_bytes(vdata);
        pkt.set_pts(pts_arr[i]);
        pkt.set_dts(dts_arr[i]);
        pkt.set_stream_index(v_idx);
        pkt.set_keyframe(i == 0);
        res = muxer.write_packet(pkt);
        assert(res.is_ok());
    }

    res = muxer.write_trailer();
    assert(res.is_ok());
    assert(writer->size() > 0);

    // Demux and verify CTTS offset extraction
    auto reader = std::make_shared<MemoryReader>(writer->data(), writer->size());
    IsoBmffDemuxer demuxer;
    res = demuxer.open(reader);
    assert(res.is_ok());

    assert(demuxer.streams().size() == 1);
    assert(demuxer.streams()[0].codec_params.codec_id == CodecId::H264);

    for (int i = 0; i < 4; ++i) {
        Packet pkt;
        res = demuxer.read_packet(pkt);
        assert(res.is_ok());
        assert(pkt.pts() == pts_arr[i]);
        assert(pkt.dts() == dts_arr[i]);
        assert(pkt.size() == 256);
        if (i == 0) {
            assert(pkt.is_keyframe());
        }
    }

    std::cout << " -> ISOBMFF muxer and CTTS B-frame tests PASSED." << std::endl;
}

void test_fragmented_mp4_demuxing() {
    std::cout << "[TEST] Running test_fragmented_mp4_demuxing..." << std::endl;

    auto writer = std::make_shared<MemoryWriter>();

    // Helper to write boxes
    auto write_box = [](MemoryWriter& w, uint32_t type, const auto& write_payload) {
        size_t start = w.tell();
        w.write_u32_be(0); // placeholder size
        w.write_fourcc(type);
        write_payload(w);
        size_t end = w.tell();
        uint32_t sz = static_cast<uint32_t>(end - start);
        // Patch size
        w.seek(start, SeekOrigin::Begin);
        w.write_u32_be(sz);
        w.seek(end, SeekOrigin::Begin);
    };

    // 1. ftyp box
    write_box(*writer, make_fourcc('f', 't', 'y', 'p'), [](MemoryWriter& w) {
        w.write_fourcc(make_fourcc('i', 's', 'o', 'm'));
        w.write_u32_be(0x00000200);
        w.write_fourcc(make_fourcc('i', 's', 'o', 'm'));
        w.write_fourcc(make_fourcc('i', 's', 'o', '6'));
    });

    // 2. moov box with mvex and trex
    write_box(*writer, make_fourcc('m', 'o', 'o', 'v'), [&](MemoryWriter& w) {
        // mvhd
        write_box(w, make_fourcc('m', 'v', 'h', 'd'), [&](MemoryWriter& sub) {
            sub.write_u32_be(0);
            sub.write_u32_be(0); sub.write_u32_be(0);
            sub.write_u32_be(1000); // timescale
            sub.write_u32_be(0);    // duration unknown / fragmented
            sub.write_u32_be(0x00010000); sub.write_u16_be(0x0100); sub.write_u16_be(0);
            sub.write_u32_be(0); sub.write_u32_be(0);
            uint32_t m[9] = {0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000};
            for (uint32_t val : m) sub.write_u32_be(val);
            for (int i = 0; i < 6; ++i) sub.write_u32_be(0);
            sub.write_u32_be(2);
        });

        // trak
        write_box(w, make_fourcc('t', 'r', 'a', 'k'), [&](MemoryWriter& sub) {
            write_box(sub, make_fourcc('t', 'k', 'h', 'd'), [&](MemoryWriter& tk) {
                tk.write_u32_be(3);
                tk.write_u32_be(0); tk.write_u32_be(0);
                tk.write_u32_be(1); // track_id = 1
                tk.write_u32_be(0); tk.write_u32_be(0); tk.write_u32_be(0); tk.write_u32_be(0);
                tk.write_u16_be(0); tk.write_u16_be(0); tk.write_u16_be(0); tk.write_u16_be(0);
                uint32_t m[9] = {0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000};
                for (uint32_t val : m) tk.write_u32_be(val);
                tk.write_u32_be(1280 << 16); tk.write_u32_be(720 << 16);
            });
            write_box(sub, make_fourcc('m', 'd', 'i', 'a'), [&](MemoryWriter& md) {
                write_box(md, make_fourcc('m', 'd', 'h', 'd'), [&](MemoryWriter& mh) {
                    mh.write_u32_be(0);
                    mh.write_u32_be(0); mh.write_u32_be(0);
                    mh.write_u32_be(1000); // timescale
                    mh.write_u32_be(0);
                    mh.write_u16_be(0x55C4); mh.write_u16_be(0);
                });
                write_box(md, make_fourcc('h', 'd', 'l', 'r'), [&](MemoryWriter& hl) {
                    hl.write_u32_be(0); hl.write_u32_be(0);
                    hl.write_fourcc(make_fourcc('v', 'i', 'd', 'e'));
                    hl.write_u32_be(0); hl.write_u32_be(0); hl.write_u32_be(0);
                    hl.write_u8(0);
                });
                write_box(md, make_fourcc('m', 'i', 'n', 'f'), [&](MemoryWriter& mf) {
                    write_box(mf, make_fourcc('s', 't', 'b', 'l'), [&](MemoryWriter& sb) {
                        write_box(sb, make_fourcc('s', 't', 's', 'd'), [&](MemoryWriter& sd) {
                            sd.write_u32_be(0); sd.write_u32_be(1);
                            write_box(sd, make_fourcc('a', 'v', 'c', '1'), [&](MemoryWriter& ent) {
                                ent.write_u32_be(0); ent.write_u16_be(0); ent.write_u16_be(1);
                                for (int i = 0; i < 4; ++i) ent.write_u32_be(0);
                                ent.write_u16_be(1280); ent.write_u16_be(720);
                                ent.write_u32_be(0x00480000); ent.write_u32_be(0x00480000);
                                ent.write_u32_be(0); ent.write_u16_be(1);
                                for (int i = 0; i < 8; ++i) ent.write_u32_be(0);
                                ent.write_u16_be(0x0018); ent.write_u16_be(0xFFFF);
                            });
                        });
                        write_box(sb, make_fourcc('s', 't', 't', 's'), [&](MemoryWriter& st) { st.write_u32_be(0); st.write_u32_be(0); });
                        write_box(sb, make_fourcc('s', 't', 's', 'c'), [&](MemoryWriter& sc) { sc.write_u32_be(0); sc.write_u32_be(0); });
                        write_box(sb, make_fourcc('s', 't', 's', 'z'), [&](MemoryWriter& sz) { sz.write_u32_be(0); sz.write_u32_be(0); sz.write_u32_be(0); });
                        write_box(sb, make_fourcc('s', 't', 'c', 'o'), [&](MemoryWriter& co) { co.write_u32_be(0); co.write_u32_be(0); });
                    });
                });
            });
        });

        // mvex with trex
        write_box(w, make_fourcc('m', 'v', 'e', 'x'), [&](MemoryWriter& mx) {
            write_box(mx, make_fourcc('t', 'r', 'e', 'x'), [&](MemoryWriter& tx) {
                tx.write_u32_be(0);
                tx.write_u32_be(1);  // track_id = 1
                tx.write_u32_be(1);  // def_desc_idx = 1
                tx.write_u32_be(40); // default_sample_duration = 40 ms (25 fps)
                tx.write_u32_be(100);// default_sample_size = 100 bytes
                tx.write_u32_be(0);  // default_sample_flags
            });
        });
    });

    // 3. moof box (Movie Fragment)
    uint32_t moof_start = static_cast<uint32_t>(writer->tell());
    write_box(*writer, make_fourcc('m', 'o', 'o', 'f'), [&](MemoryWriter& w) {
        write_box(w, make_fourcc('m', 'f', 'h', 'd'), [](MemoryWriter& mf) {
            mf.write_u32_be(0);
            mf.write_u32_be(1); // sequence_number = 1
        });
        write_box(w, make_fourcc('t', 'r', 'a', 'f'), [&](MemoryWriter& tr) {
            write_box(tr, make_fourcc('t', 'f', 'h', 'd'), [](MemoryWriter& th) {
                th.write_u32_be(0x00020000); // default flags present
                th.write_u32_be(1); // track_id = 1
            });
            write_box(tr, make_fourcc('t', 'f', 'd', 't'), [](MemoryWriter& td) {
                td.write_u32_be(0);
                td.write_u32_be(0); // baseMediaDecodeTime = 0
            });
            write_box(tr, make_fourcc('t', 'r', 'u', 'n'), [&](MemoryWriter& tu) {
                tu.write_u32_be(0x00000001); // data-offset-present
                tu.write_u32_be(2);          // sample_count = 2
                // We'll place mdat right after moof!
                tu.write_u32_be(0);          // data_offset placeholder (patched next)
            });
        });
    });
    uint32_t moof_end = static_cast<uint32_t>(writer->tell());

    // 4. mdat box with 200 bytes of sample data
    write_box(*writer, make_fourcc('m', 'd', 'a', 't'), [](MemoryWriter& w) {
        for (int i = 0; i < 200; ++i) w.write_u8(static_cast<uint8_t>(0x30 + i));
    });

    // Patch trun data_offset to point to mdat payload
    uint32_t mdat_data_offset = moof_end + 8 - moof_start;
    // Find trun data offset location in buffer
    for (size_t i = moof_start; i + 8 < moof_end; ++i) {
        uint32_t fourcc = (static_cast<uint32_t>(writer->data()[i]) << 24) |
                          (static_cast<uint32_t>(writer->data()[i+1]) << 16) |
                          (static_cast<uint32_t>(writer->data()[i+2]) << 8) |
                          static_cast<uint32_t>(writer->data()[i+3]);
        if (fourcc == make_fourcc('t', 'r', 'u', 'n')) {
            // trun header is 8 bytes + 4 bytes flags + 4 bytes sample_count + 4 bytes data_offset
            writer->seek(i + 16, SeekOrigin::Begin);
            writer->write_u32_be(mdat_data_offset);
            break;
        }
    }

    // Demux fragmented MP4
    auto reader = std::make_shared<MemoryReader>(writer->data(), writer->size());
    IsoBmffDemuxer demuxer;
    auto res = demuxer.open(reader);
    assert(res.is_ok());
    assert(demuxer.is_fragmented());

    Packet p1;
    res = demuxer.read_packet(p1);
    assert(res.is_ok());
    assert(p1.size() == 100);
    assert(p1.pts() == 0);

    Packet p2;
    res = demuxer.read_packet(p2);
    assert(res.is_ok());
    assert(p2.size() == 100);
    assert(p2.pts() == 40);

    std::cout << " -> Fragmented MP4 (fMP4) tests PASSED." << std::endl;
}

int main() {
    std::cout << "=== Running Advanced ISOBMFF (CTTS & fMP4) Test Suite ===" << std::endl;
    test_isobmff_muxer_and_ctts();
    test_fragmented_mp4_demuxing();
    std::cout << "=== All Advanced ISOBMFF tests PASSED ===" << std::endl;
    return 0;
}
