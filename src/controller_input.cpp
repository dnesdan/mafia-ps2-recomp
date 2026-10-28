#include "mafia/controller_input.hpp"

#include <array>
#include <charconv>
#include <stdexcept>
#include <utility>

namespace mafia {
namespace {
constexpr std::uint64_t maximum_time_ms = 86'400'000;
constexpr std::array<std::pair<std::string_view, std::uint16_t>, 16> buttons{{
    {"select", 0x0001}, {"l3", 0x0002}, {"r3", 0x0004}, {"start", 0x0008},
    {"up", 0x0010}, {"right", 0x0020}, {"down", 0x0040}, {"left", 0x0080},
    {"l2", 0x0100}, {"r2", 0x0200}, {"l1", 0x0400}, {"r1", 0x0800},
    {"triangle", 0x1000}, {"circle", 0x2000}, {"cross", 0x4000}, {"square", 0x8000}
}};

std::uint64_t milliseconds(std::string_view text) {
    std::uint64_t result = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size() ||
        result > maximum_time_ms)
        throw std::runtime_error("Pad event times must be decimal milliseconds within 0..86400000");
    return result;
}
} // namespace

PadEvent parse_pad_event(std::string_view specification) {
    const auto first = specification.find(':');
    const auto second = first == std::string_view::npos ? first : specification.find(':', first + 1);
    if (first == std::string_view::npos || second == std::string_view::npos ||
        specification.find(':', second + 1) != std::string_view::npos)
        throw std::runtime_error("--pad-event expects AT_MS:BUTTON:DURATION_MS");
    const auto name = specification.substr(first + 1, second - first - 1);
    PadEvent event{milliseconds(specification.substr(0, first)),
                   milliseconds(specification.substr(second + 1)), 0};
    if (event.duration_ms == 0 || event.at_ms > maximum_time_ms - event.duration_ms)
        throw std::runtime_error("Pad event duration must be positive and its end must be within 86400000 ms");
    for (const auto &[candidate, mask] : buttons) {
        if (candidate == name) {
            event.button = mask;
            return event;
        }
    }
    throw std::runtime_error("Unknown pad button: use start, select, a direction, cross, circle, square, triangle, l1, l2, l3, r1, r2 or r3");
}

std::uint16_t pad_buttons_at(std::span<const PadEvent> events, std::uint64_t elapsed_ms) noexcept {
    std::uint16_t result = 0xffff;
    for (const auto &event : events) {
        // Subtraction after the comparison cannot wrap during a long run.
        if (elapsed_ms >= event.at_ms && elapsed_ms - event.at_ms < event.duration_ms)
            result = static_cast<std::uint16_t>(result & static_cast<std::uint16_t>(~event.button));
    }
    return result;
}
} // namespace mafia
