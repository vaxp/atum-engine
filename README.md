# Atum Media Engine

> **Sovereign, Exclusively Hardware-Accelerated Multimedia Processing Engine**  
> *Written in pure C++20 for high-performance zero-copy hardware video decoding (VA-API / DRM DMA-BUF), sovereign audio decoding, container multiplexing, HDR color science, and multicore DAG scheduling.*

> [!WARNING]
> **Active Development / Experimental Status**: This project is under **active development (Work in Progress)**, remains in an **experimental stage**, and is **incomplete**. APIs, data structures, and architectural interfaces are subject to ongoing refinements and breaking changes.

---

## 1. Overview & Current Status

**Atum** is a comprehensive, sovereign **Multimedia Processing Engine** engineered from the ground up in modern **C++20** with **zero external dependencies** (no linking against FFmpeg, libavcodec, libavformat, etc.).

Named after the ancient Egyptian concept of completeness, totality, and primordial self-creation (*"The Complete One"*), **Atum** is designed as a sovereign replacement for FFmpeg, customized specifically for embedded SoCs and sovereign systems where CPU software decoding is inadequate and hardware acceleration is paramount.

### Sovereign Hardware-Accelerated Architecture
* **Pure Hardware Pipeline (No CPU Fallback Bottlenecks)**: Video decoding and encoding operate exclusively via the GPU/VPU hardware acceleration pipeline (VA-API runtime dynamic loading with DRM DMA-BUF zero-copy surface export). Pure software pixel decoding (IDCT, macroblock reconstruction, inloop deblocking, and software CABAC/CAVLC pixel crunching) has been completely excised to ensure maximum energy efficiency and high throughput on low-power host processors.
* **Sovereign Clean-Room Parameter Builders**: Dedicated, independent translation layer that maps Atum's native bitstream parsing headers into hardware execution structures (H.264, HEVC, VP9, AV1) without any third-party code contamination or legal liabilities.
* **Native Hardware Video Encoder**: Direct GPU hardware video encoding (`VaapiEncoder`) supporting H.264/HEVC with configurable GOP structures, IDR periods, CABAC, and zero-allocation GPU surface recycling (`HwSurfacePool`).
* **Streamlined Core**: All extraneous networking and RTP streaming modules have been permanently removed to minimize attack surface and binary footprint.
* **Abstracted Integration Bridges**: Integration surfaces with external rendering contexts and GUI surfaces are guarded through zero-copy buffer descriptors (`dma_buf_fd()`, `hw_surface_id()`).
* **Build & Validation Health**: The engine compiles cleanly as both shared (`.so`) and static (`.a`) libraries via Meson and Ninja. All **28 automated test suites pass with a 100% pass rate**.

---

## 2. Key Architectural Pillars

```text
┌────────────────────────────────────────────────────────────────────────┐
│                      ATUM MEDIA ENGINE ARCHITECTURE                    │
├────────────────────────────────────────────────────────────────────────┤
│  [Containers]            ISOBMFF (MP4) │ Matroska (MKV) │ TS │ IVF │ WAV│
├────────────────────────────────────────────────────────────────────────┤
│  [Hardware Video]        VA-API │ DRM DMA-BUF Zero-Copy Export         │
│  [Bitstream Parsers]     H.264/AVC │ HEVC/H.265 │ VP9 │ AV1 │ QOI Video│
├────────────────────────────────────────────────────────────────────────┤
│  [Audio Decoders]        AAC-LC │ Opus (CELT/Silk) │ FLAC │ PCM        │
│  [Audio Processing]      High-Precision Resampler │ A/V Clock Sync     │
├────────────────────────────────────────────────────────────────────────┤
│  [Color & HDR Science]   BT.709 / BT.2020 / DCI-P3 │ PQ / HLG │ ACES TM │
├────────────────────────────────────────────────────────────────────────┤
│  [Parallelism]           ThreadPool │ Frame Dependency Graph (DAG)     │
├────────────────────────────────────────────────────────────────────────┤
│  [Resilience Engine]     Error Mask │ Spatial/Temporal Concealment     │
│                          Resilient NAL Parser & Resync Hunting         │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Subsystem Capabilities

### A. Video Codecs (Exclusively Hardware Accelerated)
* **H.264 / AVC**:
  - SPS, PPS, and Slice header parsing with full VUI parameter and timing extraction.
  - Native VA-API hardware context configuration, surface allocation, and picture rendering.
  - Zero-copy DMA-BUF file descriptor export for direct zero-copy composition.
  - Resilient NAL parsing with garbage resync hunting.
* **HEVC / H.265**:
  - Main Profile and Parameter Set parsing (VPS, SPS, PPS) with conformance cropping window support.
  - Direct VA-API hardware decode pipeline and reference frame tracking via Decoded Picture Buffer (DPB).
* **VP9**:
  - Superframe unpacking and uncompressed header parsing (Profile 0/2, BT.709, BT.2020, BT.601, SMPTE170).
  - Direct hardware VA-API decode context and surface management.
* **AV1**:
  - Open Bitstream Unit (OBU) parsing with LEB128 variable-length integer decoding.
  - Sequence Header and Frame Header inspection with color description extraction.
  - Hardware acceleration probing and surface export.
* **QOI Video**:
  - Lightweight, lossless frame compression for instantaneous local caching.

---

### B. Audio Codecs & Processing
* **AAC-LC Decoder**:
  - Direct Audio Data Transport Stream (ADTS) sync-word detection and framing.
  - Huffman entropy decoding with scalefactor band reconstruction.
  - Advanced stereo tools: Mid/Side (M/S) stereo and Temporal Noise Shaping (TNS).
  - 1024-point Inverse Modified Discrete Cosine Transform (IMDCT) with selectable Sine / KBD synthesis filterbanks.
* **Opus Decoder**:
  - RFC 6716 compliant packet parsing and range decoding.
  - Fullband (48 kHz) CELT MDCT-based audio synthesis engine.
* **FLAC Decoder**:
  - Native lossless FLAC stream demuxing, subframe parsing, Linear Predictive Coding (LPC), and fixed prediction modes.
* **High-Precision Audio Resampler**:
  - Polyphase sampling-rate conversion supporting arbitrary sample rates and channel layout remapping.
* **A/V Master Clock**:
  - Microsecond-accurate audio/video synchronization clock with dynamic drift compensation.

---

### C. Container Multiplexers & Demuxers
* **ISOBMFF (MP4 / MOV)**:
  - Reading and writing standard box structures (`ftyp`, `moov`, `mvhd`, `trak`, `mdia`, `minf`, `stbl`, `stts`, `stsc`, `stsz`, `stco`, `mdat`).
  - Fast index generation and seek tables.
* **Matroska / WebM (MKV)**:
  - Complete hierarchical EBML parser and writer for cluster, block, track, and seek-head elements.
* **MPEG-TS**:
  - 188-byte packet stream synchronization, Program Specific Information (PAT/PMT) generation/demuxing, and PES packet assembly.
* **IVF & WAV**:
  - Stream containers for uncompressed PCM and raw elementary video bitstreams.
* **Unified Container Registry**:
  - Automatic format sniffing via magic-byte probing.

---

### D. Color Science, HDR & Mastering
* **Color Primaries & Gamuts**:
  - Precision $3 \times 3$ floating-point conversion matrices between ITU-R BT.709, DCI-P3, and ITU-R BT.2020.
* **Transfer Functions (OETF / EOTF)**:
  - SMPTE ST 2084 (Perceptual Quantizer - PQ) for HDR10 workflows.
  - ARIB STD-B67 (Hybrid Log-Gamma - HLG).
  - Standard sRGB, linear space, and parametric gamma curves.
* **HDR Supplemental Enhancement Information (SEI)**:
  - Extraction and application of Mastering Display Color Volume metadata (SMPTE ST 2086).
  - Content Light Level Information (CTA-861-G: MaxCLL and MaxFALL).
* **Tone Mapping Operators**:
  - HDR-to-SDR dynamic range reduction including Reinhard, ACES Film Curve, and Filmic tonemappers.
* **High Bit-Depth Pixel Surfaces**:
  - High-precision 8-bit, 10-bit, and 12-bit memory layouts and color-conversion routines.

---

### E. Parallelism & Multicore Architecture
* **Worker Thread Pool**:
  - Low-latency multi-threaded task dispatcher with prioritized work queues.
* **Frame Dependency Graph (DAG)**:
  - Asynchronous directed acyclic graph resolving Decoded Picture Buffer (DPB) dependencies for speculative, out-of-order frame decoding.

---

### F. Bitstream Resilience & Error Concealment
* **Resilient NAL Parser**:
  - Self-healing bitstream scanner that skips corrupt data chunks and resynchronizes on subsequent start codes (`0x000001` / `0x00000001`).
* **Error Concealment Mask**:
  - Block-level tracking of damaged or missing bitstream segments.
* **Spatial Error Concealment**:
  - Directional bilinear edge interpolation from adjacent healthy pixels.
* **Temporal Error Concealment**:
  - Motion-compensated projection using co-located vectors from reference frames in the DPB.

---

## 4. Building & Running Tests

The project uses **Meson** and **Ninja** with any C++20 compliant compiler (GCC 13+ or Clang 16+).

```bash
# 1. Configure the build directory
meson setup build

# 2. Compile the library and test targets
ninja -C build

# 3. Execute all 28 unit and integration test suites
meson test -C build --verbose
```

### Test Suite Summary
```text
Ok:                 28  
Expected Fail:      0   
Fail:               0   
Skipped:            0   
100% Tests Passed
```

---

## 5. Directory Structure

```text
├── include/atum/               # Public API Headers
│   ├── atum.hpp                # Primary master header
│   ├── audio/                  # Audio frames, resamplers, and synchronization clock
│   ├── bitstream/              # Bit readers, writers, and NAL unit abstractions
│   ├── codec/                  # Video and audio codec headers
│   ├── color/                  # Color gamuts, transfer functions, and tone mapping
│   ├── container/              # Demuxers, muxers, and container registry
│   ├── core/                   # Packets, frames, and core media primitives
│   ├── hwaccel/                # Hardware acceleration device abstractions (VA-API, DRM)
│   ├── parallel/               # Thread pool and Frame DAG scheduler
│   ├── predict/                # Decoded Picture Buffer (DPB) management
│   ├── resilience/             # Error masks and spatial/temporal concealment
│   └── surface/                # Video frame surfaces and color conversion
├── src/                        # Implementation sources
├── tests/                      # Automated test suite (28 test binaries)
├── reference/                  # Architectural references
│   └── ffmpeg_hwaccel/         # Upstream FFmpeg hardware acceleration source code reference
├── meson.build                 # Unified build configuration
└── README.md                   # Project documentation
```

---

## 6. Next Steps & Development Roadmap

1. **Vulkan Video Acceleration**: Expand hardware backend to support Vulkan Video decode extensions alongside VA-API.
2. **Direct DRM KMS Display Integration**: Direct plane submission from exported DMA-BUF descriptors to kernel modesetting.
