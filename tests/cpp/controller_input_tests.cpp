#include "mafia/controller_input.hpp"

#include <array>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
}

void test_controller_input() {
    const auto expect_axis = [](std::span<const mafia::PadAxisEvent> events, std::uint64_t at,
                                unsigned x, unsigned y) {
        const auto state = mafia::pad_left_stick_at(events, at);
        require(state.x == x && state.y == y, "Axis timeline chose the wrong value or boundary");
    };
    expect_axis({}, 0, 128, 128);
    const std::array axes{mafia::parse_pad_axis_event("100:lx:0:100"),
                          mafia::parse_pad_axis_event("125:left-y:255:50"),
                          mafia::parse_pad_axis_event("150:left-x:255:25")};
    expect_axis(axes, 99, 128, 128); expect_axis(axes, 100, 0, 128);
    expect_axis(axes, 125, 0, 255); expect_axis(axes, 150, 255, 255);
    expect_axis(axes, 175, 0, 128); expect_axis(axes, 200, 128, 128);
    expect_axis(axes, std::numeric_limits<std::uint64_t>::max(), 128, 128);
    const std::array reordered{axes[2], axes[0], axes[1]};
    expect_axis(reordered, 150, 255, 255); // latest start, not CLI order, wins
    const std::array tied{mafia::parse_pad_axis_event("0:lx:0:100"),
                          mafia::parse_pad_axis_event("0:left-x:96:100")};
    expect_axis(tied, 0, 96, 128); // equal starts use last CLI argument
    const std::array centered{mafia::parse_pad_axis_event("0:ly:128:1")};
    expect_axis(centered, 0, 128, 128);
    const std::array axis_edge{mafia::parse_pad_axis_event("86399999:lx:255:1")};
    expect_axis(axis_edge, 86'399'999, 255, 128);
    expect_axis(axis_edge, 86'400'000, 128, 128);
    require(mafia::parse_pad_axis_event("1:ly:0:1").axis == mafia::PadAxis::LeftY &&
            mafia::parse_pad_axis_event("1:left-x:255:1").axis == mafia::PadAxis::LeftX &&
            mafia::pad_axis_name(mafia::PadAxis::LeftX) == "lx" &&
            mafia::pad_axis_name(mafia::PadAxis::LeftY) == "ly", "Axis aliases are inconsistent");
    for (std::string_view invalid : {"", "1:lx:0", "1:lx:0:1:2", ":lx:0:1", "1::0:1",
             "1:lx::1", "1:lx:0:", "-1:lx:0:1", "1:lx:0:-1", "+1:lx:0:1", "1x:lx:0:1",
             "1:rx:0:1", "1:LX:0:1", "1:lx:-1:1", "1:lx:+1:1", "1:lx:256:1", "1:lx:1x:1",
             "1:lx:nan:1", "1:lx:1.0:1", "1:lx:4294967296:1", "1:lx:0:0", "86400000:lx:0:1",
             "1:lx:0:86400000", "18446744073709551616:lx:0:1"}) {
        bool rejected = false;
        try { (void)mafia::parse_pad_axis_event(invalid); }
        catch (const std::runtime_error &) { rejected = true; }
        require(rejected, "Malformed or overflowing analog event was accepted");
    }
    const std::array events{mafia::parse_pad_event("100:start:50"),
                            mafia::parse_pad_event("125:cross:50"),
                            mafia::parse_pad_event("160:cross:40")};
    require(mafia::pad_buttons_at(events, 99) == 0xffff, "Button pressed before its event");
    require(mafia::pad_buttons_at(events, 100) == 0xfff7, "START did not use the active-low serial mask");
    require(mafia::pad_buttons_at(events, 125) == 0xbff7, "Overlapping distinct button pulses were lost");
    expect_axis(axes, 125, 0, 255);
    require(mafia::pad_buttons_at(events, 125) == 0xbff7, "Analog sample changed simultaneous button pulses");
    require(mafia::pad_buttons_at(events, 150) == 0xbfff, "Ending START released a still-active Cross pulse");
    require(mafia::pad_buttons_at(events, 175) == 0xbfff, "Overlapping same-button pulse was released too soon");
    require(mafia::pad_buttons_at(events, 200) == 0xffff, "Ended pulse left a stuck controller button");
    require(mafia::pad_buttons_at(events, std::numeric_limits<std::uint64_t>::max()) == 0xffff,
            "Long host uptime wrapped into a stale input event");
    const std::array edge{mafia::parse_pad_event("86399999:select:1")};
    require(mafia::pad_buttons_at(edge, 86'399'999) == 0xfffe &&
            mafia::pad_buttons_at(edge, 86'400'000) == 0xffff, "Last allowed event had an incorrect release boundary");

    for (std::string_view invalid : {"", "100:start", "100:start:1:2", ":start:1", "1::1", "1:start:",
                                     "-1:start:1", "1:start:-1", "+1:start:1", "1x:start:1", "1:start:0",
                                     "1:unknown:1", "1:START:1", "86400000:start:1", "1:start:86400000",
                                     "18446744073709551616:start:1"}) {
        bool rejected = false;
        try { (void)mafia::parse_pad_event(invalid); }
        catch (const std::runtime_error &) { rejected = true; }
        require(rejected, "Malformed or overflowing controller event was accepted");
    }
}
