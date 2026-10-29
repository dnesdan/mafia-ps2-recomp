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
PadAxisEvent parse_pad_axis_event(std::string_view specification) {
    const auto first = specification.find(':');
    const auto second = first == std::string_view::npos ? first : specification.find(':', first + 1);
    const auto third = second == std::string_view::npos ? second : specification.find(':', second + 1);
    if (first == std::string_view::npos || second == std::string_view::npos ||
        third == std::string_view::npos || specification.find(':', third + 1) != std::string_view::npos)
        throw std::runtime_error("--pad-axis-event expects AT_MS:AXIS:VALUE:DURATION_MS");
    const auto name = specification.substr(first + 1, second - first - 1);
    PadAxis axis;
    if (name == "lx" || name == "left-x") axis = PadAxis::LeftX;
    else if (name == "ly" || name == "left-y") axis = PadAxis::LeftY;
    else throw std::runtime_error("Unknown pad axis: use lx or ly (left-x or left-y)");
    const auto value_text = specification.substr(second + 1, third - second - 1);
    unsigned value = 0;
    const auto [end, error] = std::from_chars(value_text.data(), value_text.data() + value_text.size(), value);
    if (value_text.empty() || error != std::errc{} || end != value_text.data() + value_text.size() || value > 255u)
        throw std::runtime_error("Pad axis value must be a decimal byte within 0..255 (128 is centered)");
    PadAxisEvent event{milliseconds(specification.substr(0, first)),
                       milliseconds(specification.substr(third + 1)), axis, static_cast<std::uint8_t>(value)};
    if (event.duration_ms == 0 || event.at_ms > maximum_time_ms - event.duration_ms)
        throw std::runtime_error("Pad axis duration must be positive and its end must be within 86400000 ms");
    return event;
}

std::string_view pad_axis_name(PadAxis axis) noexcept {
    switch (axis) {
    case PadAxis::LeftX: return "lx";
    case PadAxis::LeftY: return "ly";
    }
    return "unknown";
}

PadLeftStick pad_left_stick_at(std::span<const PadAxisEvent> events, std::uint64_t elapsed_ms) noexcept {
    PadLeftStick result;
    std::uint64_t x_start = 0, y_start = 0;
    bool have_x = false, have_y = false;
    for (const auto &event : events) {
        if (elapsed_ms < event.at_ms || elapsed_ms - event.at_ms >= event.duration_ms) continue;
        if (event.axis == PadAxis::LeftX && (!have_x || event.at_ms >= x_start)) {
            have_x = true; x_start = event.at_ms; result.x = event.value;
        } else if (event.axis == PadAxis::LeftY && (!have_y || event.at_ms >= y_start)) {
            have_y = true; y_start = event.at_ms; result.y = event.value;
        }
    }
    return result;
}
} // namespace mafia
