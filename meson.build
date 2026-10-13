project('atum-engine', 'cpp',
  version : '0.1.0',
  default_options : [
    'warning_level=3',
    'cpp_std=c++20',
    'optimization=2',
    'default_library=both',
  ]
)

atum_media_inc = include_directories('include')

atum_media_sources = files(
  'src/core/packet.cpp',
  'src/bitstream/bit_reader.cpp',
  'src/bitstream/bit_writer.cpp',
  'src/bitstream/nal_unit.cpp',
  'src/io/memory_stream.cpp',
  'src/io/file_stream.cpp',
  'src/surface/video_frame.cpp',
  'src/surface/color_convert.cpp',
  'src/audio/audio_frame.cpp',
  'src/audio/audio_resampler.cpp',
  'src/codec/codec_registry.cpp',
  'src/decode/pcm_decoder.cpp',
  'src/decode/qoi_video_decoder.cpp',
  'src/encode/pcm_encoder.cpp',
  'src/encode/qoi_video_encoder.cpp',
  'src/container/wav_container.cpp',
  'src/container/ivf_container.cpp',
  'src/container/isobmff_demuxer.cpp',
  'src/hwaccel/hwaccel.cpp',
  'src/codec/video/h264/h264_sps.cpp',
  'src/codec/video/h264/h264_pps.cpp',
  'src/codec/video/h264/h264_slice.cpp',
  'src/codec/video/h264/h264_parser.cpp',
  'src/codec/video/h264/h264_decoder.cpp',
  'src/codec/video/hevc/hevc_parameter_sets.cpp',
  'src/codec/video/hevc/hevc_slice.cpp',
  'src/codec/video/hevc/hevc_parser.cpp',
  'src/codec/video/hevc/hevc_decoder.cpp',
  'src/codec/video/vp9/vp9_header.cpp',
  'src/codec/video/vp9/vp9_parser.cpp',
  'src/codec/video/vp9/vp9_decoder.cpp',
  'src/codec/video/av1/av1_leb128.cpp',
  'src/codec/video/av1/av1_obu.cpp',
  'src/codec/video/av1/av1_parser.cpp',
  'src/codec/video/av1/av1_decoder.cpp',
  'src/audio/av_clock.cpp',
  'src/codec/audio/aac/aac_adts.cpp',
  'src/codec/audio/aac/aac_entropy.cpp',
  'src/codec/audio/aac/aac_tools.cpp',
  'src/codec/audio/aac/aac_imdct.cpp',
  'src/codec/audio/aac/aac_decoder.cpp',
  'src/codec/audio/opus/opus_parser.cpp',
  'src/codec/audio/opus/opus_range_decoder.cpp',
  'src/codec/audio/opus/celt_decoder.cpp',
  'src/codec/audio/opus/opus_decoder.cpp',
  'src/codec/audio/flac/flac_parser.cpp',
  'src/codec/audio/flac/flac_decoder.cpp',
  'src/container/mkv/ebml.cpp',
  'src/container/mkv/mkv_demuxer.cpp',
  'src/container/mkv/mkv_muxer.cpp',
  'src/container/ts/ts_demuxer.cpp',
  'src/container/ts/ts_muxer.cpp',
  'src/container/isobmff_muxer.cpp',
  'src/container/container_registry.cpp',
  'src/color/transfer_function.cpp',
  'src/color/color_gamut.cpp',
  'src/color/tone_mapper.cpp',
  'src/codec/sei_parser.cpp',
  'src/hwaccel/vaapi_device.cpp',
  'src/hwaccel/vaapi_param_builder.cpp',
  'src/hwaccel/vaapi_encoder.cpp',
  'src/predict/dpb.cpp',
  'src/parallel/thread_pool.cpp',
  'src/parallel/frame_graph.cpp',
  'src/resilience/error_mask.cpp',
  'src/resilience/spatial_concealment.cpp',
  'src/resilience/temporal_concealment.cpp',
  'src/resilience/resilient_nal_parser.cpp',
  'src/resilience/error_concealer.cpp',
)

atum_media_lib = both_libraries('atum_media',
  atum_media_sources,
  include_directories : atum_media_inc,
  install : true
)

atum_media_dep = declare_dependency(
  include_directories : atum_media_inc,
  link_with : atum_media_lib.get_static_lib()
)

# Headers installation for standalone deployments
install_subdir('include/atum', install_dir : get_option('includedir'))

# Pkg-config generation
pkg = import('pkgconfig')
pkg.generate(
  atum_media_lib,
  name : 'atum-engine',
  description : 'Atum Sovereign Zero-Dependency Multimedia Processing Engine',
  version : meson.project_version(),
  subdirs : 'atum'
)

# Automated Test Suite
subdir('tests')
