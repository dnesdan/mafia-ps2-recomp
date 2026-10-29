#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mafia {

// The lookup receives a guest pointer, never a host C string. Accept only
// bounded main-RAM mappings and require the complete name including its NUL.
inline bool is_first_mission_movie(std::span<const std::uint8_t> ram, std::uint32_t address) {
    if (address == 0u) return false;
    std::uint32_t offset;
    if (address < 0x02000000u)
        offset = address;
    else if (address >= 0x20000000u && address < 0x22000000u)
        offset = address - 0x20000000u;
    else if (address >= 0x80000000u && address < 0x82000000u)
        offset = address - 0x80000000u;
    else if (address >= 0xa0000000u && address < 0xa2000000u)
        offset = address - 0xa0000000u;
    else
        return false;
    constexpr char expected[] = "01a.pss";
    constexpr std::size_t length = sizeof(expected);
    if (offset > ram.size() || length > ram.size() - offset) return false;
    for (std::size_t i = 0; i < length; ++i) {
        const auto byte = ram[offset + i];
        const auto lower = byte >= 'A' && byte <= 'Z' ? byte + ('a' - 'A') : byte;
        if (lower != static_cast<unsigned char>(expected[i])) return false;
    }
    return true;
}

// Return PCs are valid only after the caller has verified the exact ELF hash.
class IntroSkipState {
public:
    bool should_skip(std::uint32_t return_pc, std::span<const std::uint8_t> ram = {},
                     std::uint32_t argument = 0u) {
        if (return_pc == 0x002b6454u) {
            std::uint32_t expected = 0;
            return startup_.compare_exchange_strong(expected, 1u, std::memory_order_relaxed);
        }
        if (return_pc == 0x00377900u) {
            new_game_.fetch_add(1u, std::memory_order_relaxed);
            return true;
        }
        if (return_pc == 0x001d1df0u && is_first_mission_movie(ram, argument)) {
            first_mission_.fetch_add(1u, std::memory_order_relaxed);
            return true;
        }
        return false;
    }

    std::uint32_t startup_count() const { return startup_.load(std::memory_order_relaxed); }
    std::uint32_t new_game_count() const { return new_game_.load(std::memory_order_relaxed); }
    std::uint32_t first_mission_count() const { return first_mission_.load(std::memory_order_relaxed); }

private:
    std::atomic<std::uint32_t> startup_{0}, new_game_{0}, first_mission_{0};
};

template<class Skip, class Original>
void dispatch_intro_lookup(IntroSkipState &state, std::uint32_t return_pc,
                           std::span<const std::uint8_t> ram, std::uint32_t argument,
                           Skip &&skip, Original &&original) {
    if (state.should_skip(return_pc, ram, argument)) skip();
    else original();
}

} // namespace mafia
