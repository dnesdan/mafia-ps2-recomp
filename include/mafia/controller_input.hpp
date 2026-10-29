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

enum class PadAxis : std::uint8_t { LeftX, LeftY };
struct PadAxisEvent {
    std::uint64_t at_ms = 0;
    std::uint64_t duration_ms = 0;
    PadAxis axis = PadAxis::LeftX;
    std::uint8_t value = 0x80;
};
struct PadLeftStick {
    std::uint8_t x = 0x80;
    std::uint8_t y = 0x80;
};

[[nodiscard]] PadAxisEvent parse_pad_axis_event(std::string_view specification);
[[nodiscard]] std::string_view pad_axis_name(PadAxis axis) noexcept;
// Active intervals are [start, end). The latest start wins on each axis;
// equal starts use the last command-line event. An expired override restores
// any older active event, or the neutral center when none remains.
[[nodiscard]] PadLeftStick pad_left_stick_at(std::span<const PadAxisEvent> events,
                                            std::uint64_t elapsed_ms) noexcept;

} // namespace mafia
