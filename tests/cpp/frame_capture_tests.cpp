#include "mafia/frame_capture.hpp"

#include <chrono>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F &&action) {
    bool rejected = false;
    try { action(); } catch (const std::runtime_error &) { rejected = true; }
    require(rejected, "Invalid or existing capture output was accepted");
}
std::vector<std::uint8_t> read(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
}

void test_frame_capture() {
    namespace fs = std::filesystem;
    const std::vector<std::uint8_t> rgba{
        1, 2, 3, 0, 0, 0, 0, 255,
        4, 5, 6, 17, 255, 128, 64, 32};
    const std::string header = "P6\n2 2\n255\n";
    std::vector<std::uint8_t> expected(header.begin(), header.end());
    expected.insert(expected.end(), {1, 2, 3, 0, 0, 0, 4, 5, 6, 255, 128, 64});
    const auto frame = mafia::encode_ppm_frame(rgba, 2, 2);
    require(frame.ppm == expected && frame.nonblack_pixels == 3,
            "Capture changed RGB bytes, alpha semantics, or raster row order");
    rejects([&] { mafia::encode_ppm_frame(rgba, 0, 2); });
    rejects([&] { mafia::encode_ppm_frame(rgba, 2, 3); });
    rejects([&] { mafia::encode_ppm_frame({}, 0xffffffffu, 0xffffffffu); });

    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = fs::temp_directory_path() / ("mafia-frame-test-" + std::to_string(suffix));
    require(fs::create_directory(directory), "Cannot create a fresh frame test directory");
    try {
        const auto output = directory / "capture.ppm";
        mafia::save_frame_exclusive(output, frame.ppm);
        require(read(output) == expected, "Saved capture bytes differ from encoded pixels");
        rejects([&] { mafia::save_frame_exclusive(output, {}); });
        require(read(output) == expected, "An existing capture was overwritten");
        const auto hardlink = directory / "hardlink.ppm";
        fs::create_hard_link(output, hardlink);
        rejects([&] { mafia::save_frame_exclusive(hardlink, {}); });
        require(read(output) == expected, "Capture overwrote an aliased input inode");
        const auto symlink = directory / "symlink.ppm";
        fs::create_symlink(directory / "missing.ppm", symlink);
        rejects([&] { mafia::save_frame_exclusive(symlink, frame.ppm); });
        require(!fs::exists(directory / "missing.ppm"), "Capture followed a dangling symlink");
        for (const auto &entry : fs::directory_iterator(directory))
            require(!entry.path().filename().string().starts_with(".mafia-frame-"),
                    "Capture leaked its temporary directory");
    } catch (...) {
        std::error_code error;
        fs::remove_all(directory, error);
        throw;
    }
    fs::remove_all(directory);
}
