# Roadmap

## Future Capabilities

### 1. Compute-Shader Codec Engine
GPU compute-driven decoding and encoding for professional codecs lacking dedicated ASIC hardware blocks:
- **Apple ProRes**: Compute-accelerated variable-length decoding (VLD), inverse discrete cosine transform (IDCT), and slice unpacking.
- **FFV1**: Multi-threaded GPU entropy decoding (Range Coder / Golomb-Rice) and frame reconstruction.
- Direct execution on GPU compute queues (`VK_QUEUE_COMPUTE_BIT`) keeping decoded frames in VRAM.

### 2. Video Processing & Rendering Pipeline
Real-time image manipulation and composition pipeline executed directly on decoded surfaces:
- **Color Grading**: Custom color space transformations, exposure control, and tone mapping.
- **LUT Processing**: High-precision 1D and 3D Look-Up Table (LUT) application.
- **Compositing & Blending**: Multi-track layer blending, alpha compositing, and plane mixing.
- **Visual Effects**: Spatial and temporal compute filters (convolution, blur, sharpen, noise filtering).
