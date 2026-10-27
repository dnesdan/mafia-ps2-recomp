#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif

#include "mafia/frame_capture.hpp"

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <unistd.h>

namespace mafia {

EncodedFrame encode_ppm_frame(std::span<const std::uint8_t> rgba,
                              std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0 || width > 4096 || height > 4096 ||
        std::uint64_t{width} * height * 4 != rgba.size())
        throw std::runtime_error("Cannot capture an invalid GS presentation surface");
    const std::string header = "P6\n" + std::to_string(width) + " " + std::to_string(height) + "\n255\n";
    EncodedFrame result;
    result.ppm.reserve(header.size() + std::size_t{width} * height * 3);
    result.ppm.insert(result.ppm.end(), header.begin(), header.end());
    for (std::size_t offset = 0; offset < rgba.size(); offset += 4) {
        result.ppm.insert(result.ppm.end(), rgba.begin() + static_cast<std::ptrdiff_t>(offset),
                          rgba.begin() + static_cast<std::ptrdiff_t>(offset + 3));
        if (rgba[offset] != 0 || rgba[offset + 1] != 0 || rgba[offset + 2] != 0)
            ++result.nonblack_pixels;
    }
    return result;
}

void save_frame_exclusive(const std::filesystem::path &path,
                          std::span<const std::uint8_t> ppm) {
    namespace fs = std::filesystem;
    const fs::path destination = fs::absolute(path);
    fs::create_directories(destination.parent_path());
    std::string pattern = (destination.parent_path() / ".mafia-frame-XXXXXX").string();
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back('\0');
    const char *created = ::mkdtemp(name.data());
    if (!created) throw std::runtime_error("Cannot create a private frame staging directory");
    const fs::path temporary = created;
    try {
        const auto staged = temporary / "frame.ppm";
        std::ofstream file(staged, std::ios::binary);
        file.write(reinterpret_cast<const char *>(ppm.data()), static_cast<std::streamsize>(ppm.size()));
        file.close();
        if (!file) throw std::runtime_error("Cannot write the GS frame capture");
        // A hard link publishes the already completed inode. It fails if the
        // destination exists, including a symlink introduced after validation.
        fs::create_hard_link(staged, destination);
    } catch (...) {
        std::error_code error;
        fs::remove_all(temporary, error);
        throw;
    }
    std::error_code error;
    fs::remove_all(temporary, error);
}

} // namespace mafia
