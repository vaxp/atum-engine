#include "atum/container/container_registry.hpp"
#include "atum/container/mkv/mkv_demuxer.hpp"
#include "atum/container/mkv/mkv_muxer.hpp"
#include "atum/container/ts/ts_demuxer.hpp"
#include "atum/container/ts/ts_muxer.hpp"
#include "atum/container/isobmff_demuxer.hpp"
#include "atum/container/isobmff_muxer.hpp"
#include "atum/container/wav_container.hpp"
#include "atum/container/ivf_container.hpp"

namespace atum {

ContainerFormat ContainerRegistry::probe(IoReader& reader) {
    if (!reader.is_seekable()) return ContainerFormat::Unknown;

    int64_t pos = reader.tell();
    uint8_t magic[16];
    auto res = reader.read(magic, sizeof(magic));
    reader.seek(pos, SeekOrigin::Begin);

    if (!res.is_ok() || *res < 4) {
        return ContainerFormat::Unknown;
    }

    size_t bytes_read = *res;

    // 1. Matroska / WebM EBML header: 0x1A 0x45 0xDF 0xA3
    if (bytes_read >= 4 &&
        magic[0] == 0x1A && magic[1] == 0x45 && magic[2] == 0xDF && magic[3] == 0xA3) {
        return ContainerFormat::Matroska;
    }

    // 2. MPEG-TS sync byte: 0x47 repeated every 188 bytes
    if (magic[0] == TS_SYNC_BYTE) {
        if (reader.size() >= 188 * 2) {
            reader.seek(pos + 188, SeekOrigin::Begin);
            auto b188 = reader.read_u8();
            reader.seek(pos, SeekOrigin::Begin);
            if (b188.is_ok() && *b188 == TS_SYNC_BYTE) {
                return ContainerFormat::MpegTs;
            }
        } else {
            return ContainerFormat::MpegTs;
        }
    }

    // 3. ISOBMFF / MP4 / MOV: 'ftyp', 'moov', or 'moof' at offset 4
    if (bytes_read >= 8) {
        uint32_t fourcc = (static_cast<uint32_t>(magic[4]) << 24) |
                          (static_cast<uint32_t>(magic[5]) << 16) |
                          (static_cast<uint32_t>(magic[6]) << 8)  |
                          static_cast<uint32_t>(magic[7]);
        if (fourcc == make_fourcc('f', 't', 'y', 'p') ||
            fourcc == make_fourcc('m', 'o', 'o', 'v') ||
            fourcc == make_fourcc('m', 'o', 'o', 'f')) {
            return ContainerFormat::IsoBmff;
        }
    }

    // 4. RIFF WAVE: 'RIFF' at 0, 'WAVE' at 8
    if (bytes_read >= 12 &&
        magic[0] == 'R' && magic[1] == 'I' && magic[2] == 'F' && magic[3] == 'F' &&
        magic[8] == 'W' && magic[9] == 'A' && magic[10] == 'V' && magic[11] == 'E') {
        return ContainerFormat::Wav;
    }

    // 5. IVF: 'DKIF' at 0
    if (bytes_read >= 4 &&
        magic[0] == 'D' && magic[1] == 'K' && magic[2] == 'I' && magic[3] == 'F') {
        return ContainerFormat::Ivf;
    }

    return ContainerFormat::Unknown;
}

MediaResult<std::unique_ptr<Demuxer>> ContainerRegistry::create_demuxer(std::shared_ptr<IoReader> reader) {
    if (!reader) return MediaError::IoError;

    ContainerFormat fmt = probe(*reader);
    std::unique_ptr<Demuxer> demuxer;

    switch (fmt) {
        case ContainerFormat::Matroska:
        case ContainerFormat::WebM:
            demuxer = std::make_unique<MkvDemuxer>();
            break;
        case ContainerFormat::MpegTs:
            demuxer = std::make_unique<TsDemuxer>();
            break;
        case ContainerFormat::IsoBmff:
            demuxer = std::make_unique<IsoBmffDemuxer>();
            break;
        case ContainerFormat::Wav:
            demuxer = std::make_unique<WavDemuxer>();
            break;
        case ContainerFormat::Ivf:
            demuxer = std::make_unique<IvfDemuxer>();
            break;
        default:
            return MediaError::NotImplemented;
    }

    auto res = demuxer->open(reader);
    if (!res.is_ok()) return res.error();

    return demuxer;
}

MediaResult<std::unique_ptr<Muxer>> ContainerRegistry::create_muxer(ContainerFormat format, std::shared_ptr<IoWriter> writer) {
    if (!writer) return MediaError::IoError;

    std::unique_ptr<Muxer> muxer;

    switch (format) {
        case ContainerFormat::Matroska: {
            auto m = std::make_unique<MkvMuxer>();
            m->set_doc_type("matroska");
            muxer = std::move(m);
            break;
        }
        case ContainerFormat::WebM: {
            auto m = std::make_unique<MkvMuxer>();
            m->set_doc_type("webm");
            muxer = std::move(m);
            break;
        }
        case ContainerFormat::MpegTs:
            muxer = std::make_unique<TsMuxer>();
            break;
        case ContainerFormat::IsoBmff:
            muxer = std::make_unique<IsoBmffMuxer>();
            break;
        case ContainerFormat::Wav:
            muxer = std::make_unique<WavMuxer>();
            break;
        case ContainerFormat::Ivf:
            muxer = std::make_unique<IvfMuxer>();
            break;
        default:
            return MediaError::NotImplemented;
    }

    auto res = muxer->open(writer);
    if (!res.is_ok()) return res.error();

    return muxer;
}

std::string_view ContainerRegistry::format_name(ContainerFormat format) noexcept {
    switch (format) {
        case ContainerFormat::Matroska: return "Matroska (MKV)";
        case ContainerFormat::WebM:     return "WebM";
        case ContainerFormat::MpegTs:   return "MPEG Transport Stream (TS)";
        case ContainerFormat::IsoBmff:  return "ISO Base Media File Format (MP4/MOV/fMP4)";
        case ContainerFormat::Wav:      return "RIFF Waveform Audio (WAV)";
        case ContainerFormat::Ivf:      return "Indeo Video Format (IVF)";
        default:                        return "Unknown";
    }
}

} // namespace atum
