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
    const std::array events{mafia::parse_pad_event("100:start:50"),
                            mafia::parse_pad_event("125:cross:50"),
                            mafia::parse_pad_event("160:cross:40")};
    require(mafia::pad_buttons_at(events, 99) == 0xffff, "Button pressed before its event");
    require(mafia::pad_buttons_at(events, 100) == 0xfff7, "START did not use the active-low serial mask");
    require(mafia::pad_buttons_at(events, 125) == 0xbff7, "Overlapping distinct button pulses were lost");
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
