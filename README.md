# Atum Media Engine

> **Sovereign, Exclusively Hardware-Accelerated Multimedia Processing Engine**  
> *Engineered in pure C++20 for high-performance zero-copy hardware video decoding & encoding (VA-API / DRM DMA-BUF), direct Vulkan & OpenGL GPU interop, sovereign audio decoding, container multiplexing, HDR color science, and multicore DAG scheduling.*

> [!WARNING]
> **Active Development / Experimental Status**: This project is under **active development (Work in Progress)**, remains in an **experimental stage**, and is **incomplete**. APIs, data structures, and architectural interfaces are subject to ongoing refinements and breaking changes.

---

## 1. Overview & Current Status

**Atum** is a comprehensive, sovereign **Multimedia Processing Engine** engineered from the ground up in modern **C++20**. It is tailored specifically for high-performance embedded systems, media appliances, and sovereign environments where CPU software decoding is inadequate and dedicated GPU/VPU hardware acceleration is paramount.

### Sovereign Hardware-Accelerated Architecture
* **Pure Hardware Pipeline**: Video decoding and encoding operate exclusively via the GPU/VPU hardware acceleration pipeline (VA-API runtime dynamic loading with DRM DMA-BUF zero-copy surface export). Pure software pixel decoding has been completely excised to ensure maximum energy efficiency and high throughput on host processors.
* **Direct GPU Interoperability (Vulkan & OpenGL)**: First-class zero-copy GPU integration surfaces allowing decoded video frames to be directly imported into Vulkan textures (`VkImage` via `VkSamplerYcbcrConversion`) and OpenGL textures (`EGLImage` via `GL_TEXTURE_EXTERNAL_OES`) without CPU readbacks or memory copies.
* **Sovereign Image Export & Validation**: Seamlessly integrated with the **Nisaba** image engine (`libnisaba.so`) for native, high-speed PNG image encoding directly from engine video frames for visual inspection and regression testing.
* **Sovereign Clean-Room Parameter Builders**: Dedicated, independent translation layer that maps native bitstream parsing headers into hardware execution structures (H.264, HEVC, VP9, AV1).
* **Native Hardware Video Encoder**: Direct GPU hardware video encoding (`VaapiEncoder`) supporting H.264/HEVC with configurable GOP structures, IDR periods, CABAC, and zero-allocation GPU surface recycling (`HwSurfacePool`).
* **Streamlined Core**: All extraneous networking and RTP streaming modules have been excised to minimize attack surface and binary footprint.
* **Build & Validation Health**: The engine compiles cleanly as both shared (`.so`) and static (`.a`) libraries via Meson and Ninja. All **33 automated test suites pass with a 100% pass rate**.

---

## 2. Key Architectural Pillars

```text
┌────────────────────────────────────────────────────────────────────────────────┐
│                          ATUM MEDIA ENGINE ARCHITECTURE                        │
├────────────────────────────────────────────────────────────────────────────────┤
│  [Containers]            ISOBMFF (MP4) │ Matroska (MKV) │ TS │ IVF │ WAV       │
├────────────────────────────────────────────────────────────────────────────────┤
│  [Hardware Video]        VA-API │ DRM DMA-BUF Zero-Copy Surface Export         │
│  [Bitstream Parsers]     H.264/AVC │ HEVC/H.265 │ VP9 │ AV1 │ QOI Video        │
├────────────────────────────────────────────────────────────────────────────────┤
│  [GPU Interop & Render]  Vulkan (YCbCr Samplers / DMA-BUF) │ OpenGL / EGL      │
│  [Image Export]          Nisaba Image Engine Integration (Native PNG)          │
├────────────────────────────────────────────────────────────────────────────────┤
│  [Audio Decoders]        AAC-LC │ Opus (CELT) │ FLAC │ PCM                     │
│  [Audio Processing]      High-Precision Resampler │ A/V Clock Sync             │
├────────────────────────────────────────────────────────────────────────────────┤
│  [Color & HDR Science]   BT.709 / BT.2020 / DCI-P3 │ PQ / HLG │ ACES TM        │
├────────────────────────────────────────────────────────────────────────────────┤
│  [Parallelism]           ThreadPool │ Frame Dependency Graph (DAG)             │
├────────────────────────────────────────────────────────────────────────────────┤
│  [Resilience Engine]     Error Mask │ Spatial/Temporal Concealment             │
│                          Resilient NAL Parser & Resync Hunting                 │
└────────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Subsystem Capabilities

### A. Hardware Video Acceleration & GPU Interoperability
* **VA-API & DRM DMA-BUF**:
  - Direct hardware video decoding contexts supporting H.264, HEVC, VP9, and AV1.
  - Zero-copy DMA-BUF file descriptor export (`dma_buf_fd()`) with format modifiers and multi-planar stride/offset descriptors.
  - Hardware video encoding (`VaapiEncoder`) with surface recycling pools (`HwSurfacePool`).
* **Vulkan Zero-Copy Interoperability (`atum::vk`)**:
  - Runtime dynamic dispatcher (`VulkanDispatcher`) supporting Vulkan 1.2+ without compile-time SDK lock-in.
  - Direct zero-copy importation of DMA-BUF file descriptors into `VkImage` surfaces (`VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT`).
  - Native support for multi-planar `NV12` sampling using Vulkan YCbCr conversion samplers (`VkSamplerYcbcrConversion`).
  - Robust layout handling and detiling for hardware formats (e.g., Intel Y-Tiling).
  - Vulkan Video Session and Decoder scaffolding for emerging Vulkan Video extensions.
* **OpenGL / EGL Interoperability (`atum::gl`)**:
  - Headless EGL context creation and dynamic GL dispatch (`GLDispatcher`).
  - Zero-copy DMA-BUF importation via `EGLImageKHR` (`EGL_LINUX_DMA_BUF_EXT`).
  - Sampling via `GL_TEXTURE_EXTERNAL_OES` and native 2D textures (`GLTexture`).
  - Off-screen Framebuffer Object (FBO) rendering pipeline (`GLRenderer`) for color conversion and readback.
* **Nisaba Image Export Engine Integration**:
  - Native integration with the sovereign `nisaba` image engine (`libnisaba.so`).
  - High-performance, lossless PNG export directly from `VideoFrame` instances for visual verification and testing.

---

### B. Bitstream Parsers & Video Codecs
* **H.264 / AVC**:
  - SPS, PPS, and Slice header parsing with full VUI parameter and timing extraction.
  - Native hardware context configuration and Decoded Picture Buffer (DPB) reference management.
  - Resilient NAL parsing with automatic garbage resynchronization.
* **HEVC / H.265**:
  - Main Profile and Parameter Set parsing (VPS, SPS, PPS) with conformance cropping window support.
  - Hardware decode pipeline with DPB reference index mapping.
* **VP9**:
  - Superframe unpacking and uncompressed header parsing (Profile 0/2, BT.709, BT.2020, BT.601, SMPTE170).
  - Direct hardware VA-API decode context and surface management.
* **AV1**:
  - Open Bitstream Unit (OBU) parsing with LEB128 variable-length integer decoding.
  - Sequence Header and Frame Header inspection with color description extraction.
* **QOI Video**:
  - Lightweight, lossless frame compression for instantaneous local caching.

---

### C. Audio Codecs & Processing
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

### D. Container Multiplexers & Demuxers
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

### E. Color Science, HDR & Mastering
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

### F. Parallelism & Multicore Architecture
* **Worker Thread Pool**:
  - Low-latency multi-threaded task dispatcher with prioritized work queues.
* **Frame Dependency Graph (DAG)**:
  - Asynchronous directed acyclic graph resolving Decoded Picture Buffer (DPB) dependencies for speculative, out-of-order frame decoding.

---

### G. Bitstream Resilience & Error Concealment
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

# 3. Execute all 33 unit and integration test suites
meson test -C build --verbose
```

### Test Suite Summary
```text
Ok:                 33  
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
│   ├── decode/                 # Video and audio decoders
│   ├── encode/                 # Video and audio encoders
│   ├── hwaccel/                # Hardware acceleration & GPU interop
│   │   ├── opengl/             # EGL context, GL image importer, renderer & textures
│   │   └── vulkan/             # Vulkan device, image importer, YCbCr samplers & video decoder
│   ├── io/                     # Memory and file stream abstractions
│   ├── parallel/               # Thread pool and Frame DAG scheduler
│   ├── predict/                # Decoded Picture Buffer (DPB) management
│   ├── resilience/             # Error masks and spatial/temporal concealment
│   └── surface/                # Video frames, color conversion & image export (Nisaba)
├── src/                        # Implementation sources
├── tests/                      # Automated test suite (33 test binaries)
├── assets/                     # Verification test video assets & visual exports
├── meson.build                 # Unified build configuration
└── README.md                   # Project documentation
```

---

## 6. Authorship & Maintainers

Architected and developed with pride by the **VAXP Organization** team.

---

## 7. License

This project is licensed under the **Apache License, Version 2.0** (Apache-2.0).  
See the [LICENSE](LICENSE) file for complete terms and conditions.

