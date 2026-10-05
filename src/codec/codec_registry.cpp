#include "atum/codec/codec_registry.hpp"
#include "atum/decode/pcm_decoder.hpp"
#include "atum/encode/pcm_encoder.hpp"
#include "atum/decode/qoi_video_decoder.hpp"
#include "atum/encode/qoi_video_encoder.hpp"

#include "atum/codec/video/h264/h264_decoder.hpp"
#include "atum/codec/video/hevc/hevc_decoder.hpp"
#include "atum/codec/video/vp9/vp9_decoder.hpp"
#include "atum/codec/video/av1/av1_decoder.hpp"

#include "atum/codec/audio/aac/aac_decoder.hpp"
#include "atum/codec/audio/opus/opus_decoder.hpp"
#include "atum/codec/audio/flac/flac_decoder.hpp"

namespace atum {

std::unique_ptr<VideoDecoder> CodecRegistry::create_video_decoder(CodecId id) {
    switch (id) {
        case CodecId::H264:
            return std::make_unique<H264Decoder>();
        case CodecId::H265:
            return std::make_unique<HevcDecoder>();
        case CodecId::VP9:
            return std::make_unique<Vp9Decoder>();
        case CodecId::AV1:
            return std::make_unique<Av1Decoder>();
        case CodecId::QOI_VIDEO:
            return std::make_unique<QoiVideoDecoder>();
        default:
            return nullptr;
    }
}

std::unique_ptr<AudioDecoder> CodecRegistry::create_audio_decoder(CodecId id) {
    switch (id) {
        case CodecId::PCM_S16LE:
        case CodecId::PCM_S24LE:
        case CodecId::PCM_S32LE:
        case CodecId::PCM_F32LE:
            return std::make_unique<PcmDecoder>();
        case CodecId::AAC:
            return std::make_unique<AacDecoder>();
        case CodecId::OPUS:
            return std::make_unique<OpusDecoder>();
        case CodecId::FLAC:
            return std::make_unique<FlacDecoder>();
        default:
            return nullptr;
    }
}

std::unique_ptr<VideoEncoder> CodecRegistry::create_video_encoder(CodecId id) {
    switch (id) {
        case CodecId::QOI_VIDEO:
            return std::make_unique<QoiVideoEncoder>();
        default:
            return nullptr;
    }
}

std::unique_ptr<AudioEncoder> CodecRegistry::create_audio_encoder(CodecId id) {
    switch (id) {
        case CodecId::PCM_S16LE:
        case CodecId::PCM_S24LE:
        case CodecId::PCM_S32LE:
        case CodecId::PCM_F32LE:
            return std::make_unique<PcmEncoder>();
        default:
            return nullptr;
    }
}

} // namespace atum
