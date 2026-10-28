#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace mafia {

// A host-time button pulse sampled by the serial controller device. This
// never writes a PADMAN buffer or calls a replacement EE SDK function.
struct PadEvent {
    std::uint64_t at_ms = 0;
    std::uint64_t duration_ms = 0;
    std::uint16_t button = 0;
};

[[nodiscard]] PadEvent parse_pad_event(std::string_view specification);
[[nodiscard]] std::uint16_t pad_buttons_at(std::span<const PadEvent> events,
                                         std::uint64_t elapsed_ms) noexcept;

} // namespace mafia
