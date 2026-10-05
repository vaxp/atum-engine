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
    'src/predict/chroma_pred.cpp',
    'src/network/rtp/rtp_depacketizer.cpp',
    'src/codec/video/h264/h264_forward_transform.cpp',
    'src/codec/video/motion_estimate.cpp',
    'src/codec/video/h264/h264_rate_control.cpp',
    'src/codec/video/h264/h264_rdo.cpp',
    'src/codec/video/h264/h264_entropy_encoder.cpp',
    'src/codec/video/h264/h264_encoder.cpp',
    'src/codec/screen/screen_content.cpp',
    'src/recorder/canvas_recorder.cpp',
    'src/parallel/thread_pool.cpp',
    'src/parallel/hevc_wpp.cpp',
    'src/parallel/av1_tile_grid.cpp',
    'src/parallel/frame_graph.cpp',
    'src/resilience/error_mask.cpp',
    'src/resilience/spatial_concealment.cpp',
    'src/resilience/temporal_concealment.cpp',
    'src/resilience/resilient_nal_parser.cpp',
    'src/resilience/error_concealer.cpp',
    'src/codec/video/av1/av1_partition.cpp',
    'src/codec/video/h264/h264_macroblock_syntax.cpp',
    'src/codec/video/h264/h264_residual_cabac.cpp',
    'src/codec/video/hevc/hevc_ctu_decoder.cpp',
    'src/codec/video/vp9/vp9_block_decoder.cpp',
    'src/entropy/av1_cdf_tables.cpp',
    'src/entropy/av1_symbol_decoder.cpp',
    'src/entropy/cabac.cpp',
    'src/entropy/h264_cabac_contexts.cpp',
    'src/entropy/h264_cavlc.cpp',
    'src/entropy/hevc_cabac_contexts.cpp',
    'src/entropy/vp9_bool_decoder.cpp',
    'src/entropy/vp9_probability_tables.cpp',
    'src/hwaccel/vaapi_device.cpp',
    'src/inloop/av1_film_grain.cpp',
    'src/inloop/av1_restoration.cpp',
    'src/inloop/cdef_av1.cpp',
    'src/inloop/deblock_h264.cpp',
    'src/inloop/sao_hevc.cpp',
    'src/predict/dpb.cpp',
    'src/predict/h264_inter_pred.cpp',
    'src/predict/inter_filter.cpp',
    'src/predict/intra_h264.cpp',
    'src/predict/intra_hevc.cpp',
    'src/transform/h264_transform.cpp',
    'src/transform/hevc_transform.cpp',
    'src/transform/simd_transform.cpp',
    'src/transform/vp9_transform.cpp',
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

# Automated Unit Tests
test_bitstream_exe = executable('test_bitstream',
  'tests/test_bitstream.cpp',
  dependencies : atum_media_dep
)
test('bitstream_test', test_bitstream_exe)

test_core_io_exe = executable('test_core_io',
  'tests/test_core_io.cpp',
  dependencies : atum_media_dep
)
test('core_io_test', test_core_io_exe)

test_surface_audio_exe = executable('test_surface_audio',
  'tests/test_surface_audio.cpp',
  dependencies : atum_media_dep
)
test('surface_audio_test', test_surface_audio_exe)

test_codecs_containers_exe = executable('test_codecs_containers',
  'tests/test_codecs_containers.cpp',
  dependencies : atum_media_dep
)
test('codecs_containers_test', test_codecs_containers_exe)

test_video_codecs_exe = executable('test_video_codecs',
  'tests/test_video_codecs.cpp',
  dependencies : atum_media_dep
)
test('video_codecs_test', test_video_codecs_exe)

test_aac_decoder_exe = executable('test_aac_decoder',
  'tests/test_aac_decoder.cpp',
  dependencies : atum_media_dep
)
test('aac_decoder_test', test_aac_decoder_exe)

test_opus_decoder_exe = executable('test_opus_decoder',
  'tests/test_opus_decoder.cpp',
  dependencies : atum_media_dep
)
test('opus_decoder_test', test_opus_decoder_exe)

test_flac_decoder_exe = executable('test_flac_decoder',
  'tests/test_flac_decoder.cpp',
  dependencies : atum_media_dep
)
test('flac_decoder_test', test_flac_decoder_exe)

test_av_clock_exe = executable('test_av_clock',
  'tests/test_av_clock.cpp',
  dependencies : atum_media_dep
)
test('av_clock_test', test_av_clock_exe)

test_ebml_mkv_exe = executable('test_ebml_mkv',
  'tests/test_ebml_mkv.cpp',
  dependencies : atum_media_dep
)
test('ebml_mkv_test', test_ebml_mkv_exe)

test_mpegts_exe = executable('test_mpegts',
  'tests/test_mpegts.cpp',
  dependencies : atum_media_dep
)
test('mpegts_test', test_mpegts_exe)

test_isobmff_advanced_exe = executable('test_isobmff_advanced',
  'tests/test_isobmff_advanced.cpp',
  dependencies : atum_media_dep
)
test('isobmff_advanced_test', test_isobmff_advanced_exe)

test_rtp_streaming_exe = executable('test_rtp_streaming',
  'tests/test_rtp_streaming.cpp',
  dependencies : atum_media_dep
)
test('rtp_streaming_test', test_rtp_streaming_exe)

test_container_registry_exe = executable('test_container_registry',
  'tests/test_container_registry.cpp',
  dependencies : atum_media_dep
)
test('container_registry_test', test_container_registry_exe)

test_hdr_color_science_exe = executable('test_hdr_color_science',
  'tests/test_hdr_color_science.cpp',
  dependencies : atum_media_dep
)
test('hdr_color_science_test', test_hdr_color_science_exe)

test_color_gamut_exe = executable('test_color_gamut',
  'tests/test_color_gamut.cpp',
  dependencies : atum_media_dep
)
test('color_gamut_test', test_color_gamut_exe)

test_tone_mapping_exe = executable('test_tone_mapping',
  'tests/test_tone_mapping.cpp',
  dependencies : atum_media_dep
)
test('tone_mapping_test', test_tone_mapping_exe)

test_high_bit_depth_surface_exe = executable('test_high_bit_depth_surface',
  'tests/test_high_bit_depth_surface.cpp',
  dependencies : atum_media_dep
)
test('high_bit_depth_surface_test', test_high_bit_depth_surface_exe)

test_sei_metadata_exe = executable('test_sei_metadata',
  'tests/test_sei_metadata.cpp',
  dependencies : atum_media_dep
)
test('sei_metadata_test', test_sei_metadata_exe)

test_h264_forward_transform_exe = executable('test_h264_forward_transform',
  'tests/test_h264_forward_transform.cpp',
  dependencies : atum_media_dep
)
test('h264_forward_transform_test', test_h264_forward_transform_exe)

test_motion_estimation_exe = executable('test_motion_estimation',
  'tests/test_motion_estimation.cpp',
  dependencies : atum_media_dep
)
test('motion_estimation_test', test_motion_estimation_exe)

test_h264_rate_control_exe = executable('test_h264_rate_control',
  'tests/test_h264_rate_control.cpp',
  dependencies : atum_media_dep
)
test('h264_rate_control_test', test_h264_rate_control_exe)

test_h264_entropy_encoder_exe = executable('test_h264_entropy_encoder',
  'tests/test_h264_entropy_encoder.cpp',
  dependencies : atum_media_dep
)
test('h264_entropy_encoder_test', test_h264_entropy_encoder_exe)

test_h264_encoder_pipeline_exe = executable('test_h264_encoder_pipeline',
  'tests/test_h264_encoder_pipeline.cpp',
  dependencies : atum_media_dep
)
test('h264_encoder_pipeline_test', test_h264_encoder_pipeline_exe)

test_thread_pool_exe = executable('test_thread_pool',
  'tests/test_thread_pool.cpp',
  dependencies : atum_media_dep
)
test('thread_pool_test', test_thread_pool_exe)

test_hevc_wpp_exe = executable('test_hevc_wpp',
  'tests/test_hevc_wpp.cpp',
  dependencies : atum_media_dep
)
test('hevc_wpp_test', test_hevc_wpp_exe)

test_av1_tile_grid_exe = executable('test_av1_tile_grid',
  'tests/test_av1_tile_grid.cpp',
  dependencies : atum_media_dep
)
test('av1_tile_grid_test', test_av1_tile_grid_exe)

test_frame_dependency_graph_exe = executable('test_frame_dependency_graph',
  'tests/test_frame_dependency_graph.cpp',
  dependencies : atum_media_dep
)
test('frame_dependency_graph_test', test_frame_dependency_graph_exe)

test_multicore_pipeline_benchmark_exe = executable('test_multicore_pipeline_benchmark',
  'tests/test_multicore_pipeline_benchmark.cpp',
  dependencies : atum_media_dep
)
test('multicore_pipeline_benchmark_test', test_multicore_pipeline_benchmark_exe)

test_error_mask_exe = executable('test_error_mask',
  'tests/test_error_mask.cpp',
  dependencies : atum_media_dep
)
test('error_mask_test', test_error_mask_exe)

test_spatial_error_concealment_exe = executable('test_spatial_error_concealment',
  'tests/test_spatial_error_concealment.cpp',
  dependencies : atum_media_dep
)
test('spatial_error_concealment_test', test_spatial_error_concealment_exe)

test_temporal_error_concealment_exe = executable('test_temporal_error_concealment',
  'tests/test_temporal_error_concealment.cpp',
  dependencies : atum_media_dep
)
test('temporal_error_concealment_test', test_temporal_error_concealment_exe)

test_resilient_nal_parser_exe = executable('test_resilient_nal_parser',
  'tests/test_resilient_nal_parser.cpp',
  dependencies : atum_media_dep
)
test('resilient_nal_parser_test', test_resilient_nal_parser_exe)

test_integrated_error_concealment_exe = executable('test_integrated_error_concealment',
  'tests/test_integrated_error_concealment.cpp',
  dependencies : atum_media_dep
)
test('integrated_error_concealment_test', test_integrated_error_concealment_exe)

test_av1_advanced_exe = executable('test_av1_advanced',
  'tests/test_av1_advanced.cpp',
  dependencies : atum_media_dep
)
test('av1_advanced_test', test_av1_advanced_exe)

test_entropy_exe = executable('test_entropy',
  'tests/test_entropy.cpp',
  dependencies : atum_media_dep
)
test('entropy_test', test_entropy_exe)

test_h264_cabac_syntax_exe = executable('test_h264_cabac_syntax',
  'tests/test_h264_cabac_syntax.cpp',
  dependencies : atum_media_dep
)
test('h264_cabac_syntax_test', test_h264_cabac_syntax_exe)

test_h264_cavlc_inter_exe = executable('test_h264_cavlc_inter',
  'tests/test_h264_cavlc_inter.cpp',
  dependencies : atum_media_dep
)
test('h264_cavlc_inter_test', test_h264_cavlc_inter_exe)

test_hevc_ctu_exe = executable('test_hevc_ctu',
  'tests/test_hevc_ctu.cpp',
  dependencies : atum_media_dep
)
test('hevc_ctu_test', test_hevc_ctu_exe)

test_hwaccel_exe = executable('test_hwaccel',
  'tests/test_hwaccel.cpp',
  dependencies : atum_media_dep
)
test('hwaccel_test', test_hwaccel_exe)

test_inloop_filter_exe = executable('test_inloop_filter',
  'tests/test_inloop_filter.cpp',
  dependencies : atum_media_dep
)
test('inloop_filter_test', test_inloop_filter_exe)

test_prediction_exe = executable('test_prediction',
  'tests/test_prediction.cpp',
  dependencies : atum_media_dep
)
test('prediction_test', test_prediction_exe)

test_transform_simd_exe = executable('test_transform_simd',
  'tests/test_transform_simd.cpp',
  dependencies : atum_media_dep
)
test('transform_simd_test', test_transform_simd_exe)

test_vp9_block_exe = executable('test_vp9_block',
  'tests/test_vp9_block.cpp',
  dependencies : atum_media_dep
)
test('vp9_block_test', test_vp9_block_exe)

