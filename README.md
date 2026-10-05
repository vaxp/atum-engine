# Atum Media Engine

> **Sovereign, Zero-Dependency, Multi-Format Multimedia Processing & Streaming Engine**  
> *Written in pure C++20 for high-performance audio/video decoding, encoding, container multiplexing, HDR color science, and multicore parallel processing.*

> [!WARNING]
> **Active Development / Experimental Status**: This project is under **active development (Work in Progress)**, remains in an **experimental stage**, and is **incomplete**. APIs, data structures, and architectural interfaces are subject to ongoing refinements and breaking changes.

---

## 1. Overview & Current Status

**Atum** is a comprehensive, standalone **Multimedia Processing Engine** engineered from the ground up in modern **C++20** with **zero external dependencies** (no reliance on third-party multimedia libraries, frameworks, or external decoding/encoding middleware).

Named after the ancient Egyptian concept of completeness, totality, and primordial self-creation (*"The Complete One"*), **Atum** embodies an all-inclusive, self-sufficient multimedia foundation.

### Decoupling & Standalone Architecture
* **Architectural Separation**: Originally developed as an internal media subsystem within a parent graphics engine, the media architecture grew to over **38,500 lines of code** across **230 source and header files**. To maximize modularity and performance, it has been cleanly decoupled into an independent, sovereign media engine.
* **Abstracted Integration Bridges**: All integration surfaces with external rendering contexts (such as surface blitting and rendering canvas recorders) are guarded through clean conditional header checks (`__has_include`). This guarantees that the media engine builds and executes in complete isolation (100% standalone), while retaining seamless, zero-copy hardware surface interoperability when coupled with a graphics pipeline.
* **Build & Validation Health**: The engine compiles cleanly as both shared (`.so`) and static (`.a`) libraries via Meson and Ninja. It features a comprehensive test suite of **44 unit and integration test suites**, achieving a **100% pass rate**.

---

## 2. Key Architectural Pillars

```text
┌────────────────────────────────────────────────────────────────────────┐
│                      ATUM MEDIA ENGINE ARCHITECTURE                    │
├────────────────────────────────────────────────────────────────────────┤
│  [Network / Streaming]   RTP Depacketizer & Jitter Buffer              │
├────────────────────────────────────────────────────────────────────────┤
│  [Containers]            ISOBMFF (MP4) │ Matroska (MKV) │ TS │ IVF │ WAV│
├────────────────────────────────────────────────────────────────────────┤
│  [Video Decoders]        H.264/AVC │ HEVC/H.265 │ VP9 │ AV1 │ QOI Video│
│  [Video Encoders]        Native H.264 Pipeline │ Screen Content (SCC)  │
├────────────────────────────────────────────────────────────────────────┤
│  [Audio Decoders]        AAC-LC │ Opus (CELT/Silk) │ FLAC │ PCM        │
│  [Audio Processing]      High-Precision Resampler │ A/V Clock Sync     │
├────────────────────────────────────────────────────────────────────────┤
│  [Color & HDR Science]   BT.709 / BT.2020 / DCI-P3 │ PQ / HLG │ ACES TM │
├────────────────────────────────────────────────────────────────────────┤
│  [Parallelism]           ThreadPool │ HEVC WPP │ AV1 TileGrid │ DAG    │
├────────────────────────────────────────────────────────────────────────┤
│  [Resilience Engine]     Error Mask │ Spatial/Temporal Concealment     │
├────────────────────────────────────────────────────────────────────────┤
│  [Hardware Abstraction]  VA-API Device Layer │ Zero-Copy GPU Surfaces  │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Subsystem Capabilities

### A. Video Codecs (Decoding & Encoding)
* **H.264 / AVC (Full Decoding & Native Encoding)**:
  - Full support for Baseline, Main, and High Profiles.
  - Comprehensive SPS, PPS, and Slice header parsers extracting VUI parameters, timing, and custom scaling matrices.
  - Complete entropy decoding: CAVLC and context-adaptive binary arithmetic coding (CABAC).
  - Spatial intra-prediction (4x4, 8x8, 16x16, Chroma) and temporal inter-motion compensation with sub-pixel interpolation.
  - In-loop edge-adaptive deblocking filter.
  - **Native Sovereign H.264 Encoder**: Complete encoding pipeline including forward 4x4 integer transform and quantization, multi-pattern motion estimation (Diamond, Hexagonal, Full-Pel, and Half-Pel refinement), flexible rate control (CQP and CBR), rate-distortion optimization (RDO), and Intra Block Copy (IntraBC) for screen content coding.
* **HEVC / H.265 (Decoding)**:
  - Main Profile support with parameter set parsers (VPS, SPS, PPS) and independent/dependent slice segments.
  - Coding Tree Unit (CTU) quadtree recursive parsing (CU, PU, TU).
  - 35-direction angular intra-prediction and advanced motion vector prediction (AMVP / Merge mode).
  - Sample Adaptive Offset (SAO) in-loop filtering.
* **VP9 (Decoding)**:
  - Superframe and frame uncompressed header parsing with adaptive boolean arithmetic entropy decoding.
  - Variable transform block processing (4x4 up to 32x32) with dynamic probability context adaptation.
* **AV1 (Advanced Decoding)**:
  - Open Bitstream Unit (OBU) parsing with LEB128 variable-length integer decoding.
  - Multi-symbol non-binary arithmetic decoding driving adaptive Cumulative Distribution Function (CDF) tables.
  - Constrained Directional Enhancement Filtering (CDEF) and loop restoration.
  - Synthetic film grain parameter parsing and rendering.
* **QOI Video**:
  - Ultra-fast, lightweight lossless frame-sequence compression tailored for low-latency surface caching.

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
The engine implements native demuxers and muxers operating directly on raw streams without third-party libraries:
* **ISOBMFF (MP4 / MOV)**:
  - Reading and writing standard box structures (`ftyp`, `moov`, `mvhd`, `trak`, `mdia`, `minf`, `stbl`, `stts`, `stsc`, `stsz`, `stco`, `mdat`).
  - Index generation and fast random-access seeking.
* **Matroska / WebM (MKV)**:
  - Complete hierarchical EBML parser and writer for cluster, block, track, and seek-head elements.
* **MPEG-TS**:
  - 188-byte packet stream synchronization, Program Specific Information (PAT/PMT) generation/demuxing, and PES packet assembly.
* **IVF & WAV**:
  - Stream containers for uncompressed PCM and raw elementary video bitstreams.
* **Unified Container Registry**:
  - Automatic format sniffing via initial magic-byte probing.

---

### D. Color Science, HDR & Mastering
* **Color Primaries & Gamuts**:
  - Precision $3 \times 3$ floating-point conversion matrices between ITU-R BT.709, DCI-P3, and ITU-R BT.2020.
* **Opto-Electronic & Electro-Optical Transfer Functions (OETF / EOTF)**:
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
  - Low-latency multi-threaded task dispatcher.
* **HEVC Wavefront Parallel Processing (WPP)**:
  - Concurrent CTU row decoding across cores honoring the 2-CTU diagonal CABAC state dependency.
* **AV1 Tile Grid Dispatcher**:
  - Independent spatial tile partition distribution across available worker threads.
* **Frame Dependency Graph (DAG)**:
  - Asynchronous directed acyclic graph resolving Decoded Picture Buffer (DPB) dependencies for speculative, out-of-order frame decoding.

---

### F. Network Resilience & Error Concealment
* **Error Concealment Mask**:
  - Macroblock/block-level tracking of missing or corrupt bitstream packets.
* **Spatial Error Concealment**:
  - Directional edge interpolation from adjacent healthy pixels to conceal transmission gaps.
* **Temporal Error Concealment**:
  - Motion-compensated projection using co-located vectors from reference frames in the DPB.
* **Resilient NAL Parser**:
  - Self-healing bitstream scanner that skips corrupt data chunks and resynchronizes on subsequent start codes (`0x000001` / `0x00000001`).

---

### G. Network Streaming & Surface Recording
* **RTP Streaming**:
  - Real-time Transport Protocol depacketizer featuring packet reordering, loss detection, and jitter buffering.
* **Surface Recorder**:
  - Real-time capturing and encoding of 32-bit RGBA surfaces into H.264 streams packaged directly into MP4 or MKV containers.

---

## 4. Building & Running Tests

The project uses **Meson** and **Ninja** with any C++20 compliant compiler (GCC 13+ or Clang 16+).

```bash
# 1. Configure the build directory
meson setup build

# 2. Compile the library and test targets
ninja -C build

# 3. Execute all 44 unit and integration test suites
ninja -C build test
```

### Test Suite Summary
```text
Ok:                 44  
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
│   ├── codec/                  # Video and audio codec implementations
│   ├── color/                  # Color gamuts, transfer functions, and tone mapping
│   ├── container/              # Demuxers, muxers, and container registry
│   ├── core/                   # Packets, frames, and core media primitives
│   ├── hwaccel/                # Hardware acceleration device abstractions
│   ├── parallel/               # Thread pool, WPP, Tile Grid, and Frame DAG
│   ├── recorder/               # Surface and screen recording pipeline
│   ├── resilience/             # Error masks and spatial/temporal concealment
│   └── surface/                # Video frame surfaces and color conversion
├── src/                        # Implementation sources (105 C++ files)
├── tests/                      # Comprehensive test suite (44 test binaries)
├── meson.build                 # Unified build configuration
└── README.md                   # Project documentation
```

---

## 6. Next Steps & Development Roadmap

1. **Hardware Acceleration Expansion**: Broaden direct hardware interfaces (e.g., Vulkan Video and native VA-API pipelines).
2. **Additional Audio Encoders**: Complement the native PCM encoder with integrated lossy and compressed audio encoders.
3. **Network Streaming Protocols**: Expand RTP foundation with RTMP/SRT streaming support.

