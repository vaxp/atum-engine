#include "atum/container/container_registry.hpp"
#include "atum/container/mkv/mkv_muxer.hpp"
#include "atum/io/memory_stream.hpp"
#include <cassert>
#include <iostream>
#include <vector>

using namespace atum;

void test_container_probing() {
    std::cout << "[TEST] Running test_container_probing..." << std::endl;

    // 1. Matroska / WebM EBML header: 0x1A 0x45 0xDF 0xA3
    uint8_t mkv_hdr[] = {0x1A, 0x45, 0xDF, 0xA3, 0x9F, 0x42, 0x86, 0x81, 0x01};
    MemoryReader mkv_r(mkv_hdr, sizeof(mkv_hdr));
    assert(ContainerRegistry::probe(mkv_r) == ContainerFormat::Matroska);

    // 2. MPEG-TS sync byte: 0x47 repeated every 188 bytes
    std::vector<uint8_t> ts_stream(188 * 3, 0xFF);
    ts_stream[0] = 0x47;
    ts_stream[188] = 0x47;
    ts_stream[376] = 0x47;
    MemoryReader ts_r(ts_stream.data(), ts_stream.size());
    assert(ContainerRegistry::probe(ts_r) == ContainerFormat::MpegTs);

    // 3. ISOBMFF: 'ftyp' at offset 4
    uint8_t mp4_hdr[] = {0x00, 0x00, 0x00, 0x20, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm'};
    MemoryReader mp4_r(mp4_hdr, sizeof(mp4_hdr));
    assert(ContainerRegistry::probe(mp4_r) == ContainerFormat::IsoBmff);

    // 4. WAV: 'RIFF' at 0, 'WAVE' at 8
    uint8_t wav_hdr[] = {'R', 'I', 'F', 'F', 0x24, 0x00, 0x00, 0x00, 'W', 'A', 'V', 'E'};
    MemoryReader wav_r(wav_hdr, sizeof(wav_hdr));
    assert(ContainerRegistry::probe(wav_r) == ContainerFormat::Wav);

    // 5. IVF: 'DKIF' at 0
    uint8_t ivf_hdr[] = {'D', 'K', 'I', 'F', 0x00, 0x00, 0x20, 0x00};
    MemoryReader ivf_r(ivf_hdr, sizeof(ivf_hdr));
    assert(ContainerRegistry::probe(ivf_r) == ContainerFormat::Ivf);

    std::cout << " -> Container sniffing tests PASSED." << std::endl;
}

void test_registry_factories() {
    std::cout << "[TEST] Running test_registry_factories..." << std::endl;

    auto writer = std::make_shared<MemoryWriter>();
    auto mux_res = ContainerRegistry::create_muxer(ContainerFormat::Matroska, writer);
    assert(mux_res.is_ok());
    assert(*mux_res != nullptr);

    auto ts_mux_res = ContainerRegistry::create_muxer(ContainerFormat::MpegTs, writer);
    assert(ts_mux_res.is_ok());
    assert(*ts_mux_res != nullptr);

    auto mp4_mux_res = ContainerRegistry::create_muxer(ContainerFormat::IsoBmff, writer);
    assert(mp4_mux_res.is_ok());
    assert(*mp4_mux_res != nullptr);

    // Verify human readable names
    assert(ContainerRegistry::format_name(ContainerFormat::Matroska) == "Matroska (MKV)");
    assert(ContainerRegistry::format_name(ContainerFormat::MpegTs) == "MPEG Transport Stream (TS)");
    assert(ContainerRegistry::format_name(ContainerFormat::IsoBmff) == "ISO Base Media File Format (MP4/MOV/fMP4)");

    std::cout << " -> ContainerRegistry factory tests PASSED." << std::endl;
}

int main() {
    std::cout << "=== Running Container Registry Test Suite ===" << std::endl;
    test_container_probing();
    test_registry_factories();
    std::cout << "=== All Container Registry tests PASSED ===" << std::endl;
    return 0;
}
