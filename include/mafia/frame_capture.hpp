#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace mafia {

struct EncodedFrame {
    std::vector<std::uint8_t> ppm;
    std::uint64_t nonblack_pixels{};
};

// Preserve the supplied raster's row order and RGB bytes. PPM has no alpha
// channel, scaling, display-aspect correction, overlays, or color correction.
EncodedFrame encode_ppm_frame(std::span<const std::uint8_t> rgba,
                              std::uint32_t width, std::uint32_t height);

// Publish a complete file atomically, without replacing an existing path.
// Fresh filenames prevent an earlier run's image from becoming new evidence.
void save_frame_exclusive(const std::filesystem::path &path,
                          std::span<const std::uint8_t> ppm);

} // namespace mafia
