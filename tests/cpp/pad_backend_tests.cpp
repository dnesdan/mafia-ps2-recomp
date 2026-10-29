#include "runtime/ps2_pad.h"
#include "raylib.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <set>
#include <utility>
#include <stdexcept>

namespace {
std::set<int> keys, buttons;
std::array<float, 4> axes{};
bool gamepad = false;
unsigned keyboard_queries = 0;
void require(bool ok, const char *text) { if (!ok) throw std::runtime_error(text); }
std::array<std::uint8_t, 40> sample(std::initializer_list<int> pressed = {}) {
    keys = pressed; keyboard_queries = 0;
    std::array<std::uint8_t, 40> data; data.fill(0xA5);
    PSPadBackend backend;
    require(backend.readState(0, 0, data.data(), data.size()), "backend rejected owned output");
    require(std::all_of(data.begin() + 32, data.end(), [](auto byte) { return byte == 0xA5; }),
            "backend overwrote output guard");
    require(data[0] == 1 && data[1] == 0x73, "pad markers changed");
    return data;
}
std::uint16_t mask(const std::array<std::uint8_t, 40> &data) {
    return static_cast<std::uint16_t>(data[2] | (std::uint16_t(data[3]) << 8));
}
void expectAxes(std::initializer_list<int> pressed, unsigned x, unsigned y) {
    const auto data = sample(pressed);
    require(data[6] == x && data[7] == y, "keyboard left axis differs");
    require(data[4] == 128 && data[5] == 128, "keyboard changed the right stick");
    require((mask(data) & 0xF0u) == 0xF0u, "WASD also pressed the D-pad");
}
}
extern "C" bool IsGamepadAvailable(int device) { return device == 0 && gamepad; }
extern "C" bool IsGamepadButtonDown(int device, int button) { return device == 0 && buttons.contains(button); }
extern "C" float GetGamepadAxisMovement(int device, int axis) {
    if (device != 0) throw std::runtime_error("changed gamepad priority");
    switch (axis) {
    case GAMEPAD_AXIS_LEFT_X: return axes[0];
    case GAMEPAD_AXIS_LEFT_Y: return axes[1];
    case GAMEPAD_AXIS_RIGHT_X: return axes[2];
    case GAMEPAD_AXIS_RIGHT_Y: return axes[3];
    default: throw std::runtime_error("unexpected gamepad axis");
    }
}
extern "C" bool IsKeyDown(int key) { ++keyboard_queries; return keys.contains(key); }
int main() {
    try {
        const auto neutral = sample();
        require(mask(neutral) == 0xFFFF, "neutral buttons differ");
        expectAxes({}, 128, 128);
        expectAxes({KEY_A}, 0, 128); expectAxes({KEY_D}, 255, 128);
        expectAxes({KEY_W}, 128, 0); expectAxes({KEY_S}, 128, 255);
        expectAxes({KEY_A, KEY_D}, 128, 128); expectAxes({KEY_W, KEY_S}, 128, 128);
        expectAxes({KEY_A, KEY_D, KEY_W}, 128, 0);
        expectAxes({KEY_W, KEY_D}, 255, 0);
        expectAxes({}, 128, 128); // release after a diagonal, no sticky axes
        for (const auto &[key, bit] : std::array<std::pair<int, unsigned>, 4>{{
                 {KEY_UP, 0x10}, {KEY_RIGHT, 0x20}, {KEY_DOWN, 0x40}, {KEY_LEFT, 0x80}}}) {
            const auto data = sample({key});
            require(mask(data) == (0xFFFFu & ~bit) && data[6] == 128 && data[7] == 128,
                    "arrow D-pad changed analog or another button");
        }
        for (int key : {KEY_X, KEY_SPACE})
            require(mask(sample({key})) == 0xBFFF, "Cross alias changed");
        require(mask(sample({KEY_C})) == 0xDFFF, "Circle key changed");
        require(mask(sample({KEY_ESCAPE})) == 0xFFFF, "Escape still presses Circle");
        const auto chord = sample({KEY_W, KEY_A, KEY_UP, KEY_X, KEY_LEFT_CONTROL, KEY_RIGHT_CONTROL});
        require(chord[6] == 0 && chord[7] == 0 && mask(chord) == 0xBFE9,
                "steering, D-pad, Cross and thumb clicks do not coexist");
        require(mask(sample({KEY_ENTER, KEY_TAB, KEY_Q, KEY_E, KEY_LEFT_SHIFT, KEY_RIGHT_SHIFT})) == 0xF0F6,
                "Start/Select/shoulder keys changed");
        gamepad = true; axes = {-1.0f, 1.0f, 0.5f, -0.5f};
        buttons = {GAMEPAD_BUTTON_RIGHT_FACE_DOWN, GAMEPAD_BUTTON_LEFT_THUMB};
        const auto physical = sample({KEY_A, KEY_W, KEY_C, KEY_RIGHT_CONTROL});
        require(physical[6] == 1 && physical[7] == 255 && physical[4] == 191 && physical[5] == 64,
                "physical gamepad axis mapping changed");
        require(mask(physical) == 0xBFFD && keyboard_queries == 0,
                "keyboard overrode the connected gamepad");
        gamepad = false; buttons.clear();
        expectAxes({}, 128, 128); // unplug restores a fresh keyboard sample
        PSPadBackend backend;
        auto guard = neutral;
        require(!backend.readState(0, 0, nullptr, 32) && !backend.readState(0, 0, guard.data(), 31) &&
                    guard == neutral, "invalid backend output mutated bytes");
        std::cout << "Keyboard axes, aliases, thumb clicks and unchanged gamepad priority passed.\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
