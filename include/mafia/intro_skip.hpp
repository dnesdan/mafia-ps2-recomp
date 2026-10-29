#pragma once

#include <atomic>
#include <cstdint>

namespace mafia {

// Return PCs are valid only after the caller has verified the exact ELF hash.
class IntroSkipState {
public:
    bool should_skip(std::uint32_t return_pc) {
        if (return_pc == 0x002b6454u) {
            std::uint32_t expected = 0;
            return startup_.compare_exchange_strong(expected, 1u, std::memory_order_relaxed);
        }
        if (return_pc == 0x00377900u) {
            new_game_.fetch_add(1u, std::memory_order_relaxed);
            return true;
        }
        return false;
    }

    std::uint32_t startup_count() const { return startup_.load(std::memory_order_relaxed); }
    std::uint32_t new_game_count() const { return new_game_.load(std::memory_order_relaxed); }

private:
    std::atomic<std::uint32_t> startup_{0}, new_game_{0};
};

} // namespace mafia
