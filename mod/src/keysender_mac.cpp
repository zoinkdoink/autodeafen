#include <Geode/DefaultInclude.hpp>

#ifdef GEODE_IS_MACOS

#include "keysender.hpp"

#include <Geode/loader/Log.hpp>

// CoreGraphics only: the ApplicationServices umbrella pulls in CarbonCore,
// whose legacy CommentType typedef collides with the GD bindings' enum.
#include <CoreGraphics/CoreGraphics.h>

#include <string>
#include <unordered_map>

using namespace geode::prelude;

namespace {

// ANSI-layout virtual key codes (Carbon kVK_*). F21-F24 have no Mac key.
std::optional<CGKeyCode> keyCodeFor(std::string const& key) {
    static const std::unordered_map<std::string, CGKeyCode> codes = {
        {"a", 0},   {"b", 11},  {"c", 8},   {"d", 2},   {"e", 14},  {"f", 3},
        {"g", 5},   {"h", 4},   {"i", 34},  {"j", 38},  {"k", 40},  {"l", 37},
        {"m", 46},  {"n", 45},  {"o", 31},  {"p", 35},  {"q", 12},  {"r", 15},
        {"s", 1},   {"t", 17},  {"u", 32},  {"v", 9},   {"w", 13},  {"x", 7},
        {"y", 16},  {"z", 6},
        {"0", 29},  {"1", 18},  {"2", 19},  {"3", 20},  {"4", 21},  {"5", 23},
        {"6", 22},  {"7", 26},  {"8", 28},  {"9", 25},
        {"f1", 122},  {"f2", 120},  {"f3", 99},   {"f4", 118},  {"f5", 96},
        {"f6", 97},   {"f7", 98},   {"f8", 100},  {"f9", 101},  {"f10", 109},
        {"f11", 103}, {"f12", 111}, {"f13", 105}, {"f14", 107}, {"f15", 113},
        {"f16", 106}, {"f17", 64},  {"f18", 79},  {"f19", 80},  {"f20", 90},
        {"space", 49},     {"enter", 36},    {"tab", 48},      {"backspace", 51},
        {"delete", 117},   {"esc", 53},      {"home", 115},    {"end", 119},
        {"pageup", 116},   {"pagedown", 121},{"up", 126},      {"down", 125},
        {"left", 123},     {"right", 124},
        {"minus", 27},     {"equal", 24},    {"comma", 43},    {"period", 47},
        {"slash", 44},     {"semicolon", 41},{"apostrophe", 39},
        {"bracketleft", 33},{"bracketright", 30},{"backslash", 42},{"grave", 50},
    };
    auto it = codes.find(key);
    if (it != codes.end()) return it->second;
    return std::nullopt;
}

bool ensurePostEventAccess() {
    if (CGPreflightPostEventAccess()) return true;
    static bool requested = false;
    if (!requested) {
        requested = true;
        CGRequestPostEventAccess();
        log::warn(
            "AutoDeafen: macOS Accessibility permission missing. Grant it to "
            "Geometry Dash in System Settings > Privacy & Security > "
            "Accessibility.");
    }
    return false;
}

} // namespace

namespace autodeafen {

bool runningUnderWine() {
    return false;
}

void prepareKeystrokeMode() {
    ensurePostEventAccess();
}

void tapKey(KeySpec const& spec) {
    if (!ensurePostEventAccess()) return;
    auto code = keyCodeFor(spec.key);
    if (!code) {
        log::warn("AutoDeafen: key '{}' has no macOS mapping", spec.key);
        return;
    }
    // Known limitation, verified empirically: desktop Discord's macOS global
    // keybinds listen at the raw HID device level (Input Monitoring), which
    // synthetic Quartz events can never reach — neither CGEventPost with a
    // HID-state source nor CGEventPostToPid gets through. The tap is still
    // posted for any listener that does see Quartz events, but Discord users
    // on macOS need the discord delivery mode.
    static bool warned = false;
    if (!warned) {
        warned = true;
        log::warn("AutoDeafen: desktop Discord on macOS cannot see synthetic "
                  "key events — use the discord delivery mode instead");
    }
    CGEventFlags flags = 0;
    if (spec.ctrl) flags |= kCGEventFlagMaskControl;
    if (spec.shift) flags |= kCGEventFlagMaskShift;
    if (spec.alt) flags |= kCGEventFlagMaskAlternate;
    if (spec.super) flags |= kCGEventFlagMaskCommand;

    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    for (bool down : {true, false}) {
        CGEventRef event = CGEventCreateKeyboardEvent(source, *code, down);
        if (!event) {
            log::warn("AutoDeafen: failed to create keyboard event");
            break;
        }
        if (flags) {
            CGEventSetFlags(event, flags | CGEventGetFlags(event));
        }
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
    }
    if (source) CFRelease(source);
}

} // namespace autodeafen

#endif // GEODE_IS_MACOS
