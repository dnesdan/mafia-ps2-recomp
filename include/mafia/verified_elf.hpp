#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mafia {

struct LoadSegment {
    std::uint32_t offset{};
    std::uint32_t virtual_address{};
    std::uint32_t file_size{};
    std::uint32_t memory_size{};
    std::uint32_t flags{};
};

struct VerifiedElf {
    std::vector<std::uint8_t> bytes;
    std::string sha256;
    std::uint32_t entry{};
    std::vector<LoadSegment> segments;
};

std::string sha256(std::span<const std::uint8_t> bytes);
VerifiedElf verify_elf(std::span<const std::uint8_t> bytes,
                       std::string_view expected_sha256,
                       std::uint32_t expected_entry);
VerifiedElf read_verified_elf(const std::filesystem::path &path,
                            std::string_view expected_sha256,
                            std::uint32_t expected_entry);
std::string json_string(std::string_view value);

} // namespace mafia
