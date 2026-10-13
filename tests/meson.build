# Sovereign Atum Engine - Automated Test Suite
# All unit and integration test definitions

test_cases = [
  'bitstream',
  'core_io',
  'surface_audio',
  'codecs_containers',
  'video_codecs',
  'aac_decoder',
  'opus_decoder',
  'flac_decoder',
  'av_clock',
  'ebml_mkv',
  'mpegts',
  'isobmff_advanced',
  'container_registry',
  'hdr_color_science',
  'color_gamut',
  'tone_mapping',
  'high_bit_depth_surface',
  'sei_metadata',
  'thread_pool',
  'frame_dependency_graph',
  'error_mask',
  'spatial_error_concealment',
  'temporal_error_concealment',
  'resilient_nal_parser',
  'integrated_error_concealment',
  'hwaccel',
  'hw_param_builder',
  'vaapi_encoder',
  'real_assets',
]

foreach t : test_cases
  t_exe = executable(
    'test_' + t,
    'test_' + t + '.cpp',
    dependencies : atum_media_dep
  )
  test(
    t + '_test',
    t_exe,
    workdir : meson.project_source_root()
  )
endforeach
