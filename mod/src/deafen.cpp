#include "deafen.hpp"

#include "core/keyspec.hpp"
#include "discord_rpc.hpp"
#include "keysender.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace autodeafen::deafen {

namespace {

// GD's enumKeyCodes -> canonical key grammar name. Mostly VK values, with
// RobTop quirks: OEMPlus is 0xB8 (not VK's 0xBB) and arrows exist both as
// 0x25-0x28 and as KEY_Arrow* (0x11B-0x11E).
std::optional<std::string> keyNameFromCode(int code) {
    if (code >= 0x41 && code <= 0x5A) return std::string(1, char('a' + (code - 0x41)));
    if (code >= 0x30 && code <= 0x39) return std::string(1, char('0' + (code - 0x30)));
    if (code >= 0x70 && code <= 0x87) return "f" + std::to_string(code - 0x70 + 1);
    switch (code) {
        case 0x20: return "space";
        case 0x0D: return "enter";
        case 0x09: return "tab";
        case 0x08: return "backspace";
        case 0x2E: return "delete";
        case 0x24: return "home";
        case 0x23: return "end";
        case 0x21: return "pageup";
        case 0x22: return "pagedown";
        case 0x26: case 0x11B: return "up";
        case 0x28: case 0x11C: return "down";
        case 0x25: case 0x11D: return "left";
        case 0x27: case 0x11E: return "right";
        case 0xBD: return "minus";
        case 0xB8: case 0xBB: return "equal";
        case 0xBC: return "comma";
        case 0xBE: return "period";
        case 0xBF: return "slash";
        case 0xBA: return "semicolon";
        case 0xDE: return "apostrophe";
        case 0xDB: return "bracketleft";
        case 0xDD: return "bracketright";
        case 0xDC: return "backslash";
        case 0xC0: return "grave";
        // Geode's normalized punctuation codes (enumKeyCodes "Geode additions")
        case 0x1000: return "grave";
        case 0x1001: case 0x1008: return "equal";
        case 0x1002: return "bracketleft";
        case 0x1003: return "bracketright";
        case 0x1004: return "backslash";
        case 0x1005: return "semicolon";
        case 0x1006: return "apostrophe";
        case 0x1007: return "slash";
        case 0x1009: return "enter";
        default: return std::nullopt;
    }
}

std::optional<KeySpec> specFromKeybind(geode::Keybind const& bind) {
    auto name = keyNameFromCode(int(bind.key));
    if (!name) return std::nullopt;
    KeySpec spec;
    spec.ctrl = bind.modifiers & KeyboardModifier::Control;
    spec.shift = bind.modifiers & KeyboardModifier::Shift;
    spec.alt = bind.modifiers & KeyboardModifier::Alt;
    spec.super = bind.modifiers & KeyboardModifier::Super;
    spec.key = *name;
    return spec;
}

} // namespace

void request(bool deafened) {
    if (Mod::get()->getSettingValue<std::string>("delivery-mode") != "keystroke") {
        rpc::requestDeafen(deafened);
        return;
    }

    // Keystroke mode: the deafen key is a toggle, so track what we believe
    // Discord's state to be. Can desync if the user toggles manually; since
    // we always un-deafen at attempt end, it self-corrects every attempt.
    static bool s_believedDeafened = false;
    if (deafened == s_believedDeafened) return;
    auto binds = Mod::get()->getSettingValue<std::vector<geode::Keybind>>("keybind");
    if (binds.empty()) {
        log::warn("AutoDeafen: no deafen key bound, not sending");
        return;
    }
    auto spec = specFromKeybind(binds.front());
    if (!spec) {
        log::warn("AutoDeafen: bound key {} has no sendable mapping",
                  binds.front().toString());
        return;
    }
    s_believedDeafened = deafened;
    log::info("AutoDeafen: tapping key ({})", deafened ? "deafen" : "un-deafen");
    tapKey(*spec);
}

} // namespace autodeafen::deafen
