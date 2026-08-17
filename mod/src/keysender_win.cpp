#include <Geode/DefaultInclude.hpp>

#ifdef GEODE_IS_WINDOWS

#include "keysender.hpp"

#include <Geode/loader/Log.hpp>

#include <windows.h>

#include <cstdlib>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace geode::prelude;

namespace {

std::optional<WORD> vkFor(std::string const& key) {
    if (key.size() == 1) {
        char c = key[0];
        if (c >= 'a' && c <= 'z') return WORD(0x41 + (c - 'a'));
        if (c >= '0' && c <= '9') return WORD(0x30 + (c - '0'));
    }
    if (key.size() >= 2 && key[0] == 'f') {
        int n = std::atoi(key.c_str() + 1);
        if (n >= 1 && n <= 24) return WORD(VK_F1 + n - 1);
    }
    static const std::unordered_map<std::string, WORD> named = {
        {"space", VK_SPACE},        {"enter", VK_RETURN},
        {"tab", VK_TAB},            {"backspace", VK_BACK},
        {"delete", VK_DELETE},      {"esc", VK_ESCAPE},
        {"home", VK_HOME},          {"end", VK_END},
        {"pageup", VK_PRIOR},       {"pagedown", VK_NEXT},
        {"up", VK_UP},              {"down", VK_DOWN},
        {"left", VK_LEFT},          {"right", VK_RIGHT},
        {"minus", VK_OEM_MINUS},    {"equal", VK_OEM_PLUS},
        {"comma", VK_OEM_COMMA},    {"period", VK_OEM_PERIOD},
        {"slash", VK_OEM_2},        {"semicolon", VK_OEM_1},
        {"apostrophe", VK_OEM_7},   {"bracketleft", VK_OEM_4},
        {"bracketright", VK_OEM_6}, {"backslash", VK_OEM_5},
        {"grave", VK_OEM_3},
    };
    auto it = named.find(key);
    if (it != named.end()) return it->second;
    return std::nullopt;
}

// Keys whose scancodes need KEYEVENTF_EXTENDEDKEY (the non-numpad variants).
bool isExtendedKey(std::string const& key) {
    return key == "delete" || key == "home" || key == "end"
        || key == "pageup" || key == "pagedown"
        || key == "up" || key == "down" || key == "left" || key == "right";
}

void tapNative(autodeafen::KeySpec const& spec) {
    auto vk = vkFor(spec.key);
    if (!vk) {
        log::warn("AutoDeafen: key '{}' has no Windows mapping", spec.key);
        return;
    }
    std::vector<WORD> modifiers;
    if (spec.ctrl) modifiers.push_back(VK_CONTROL);
    if (spec.shift) modifiers.push_back(VK_SHIFT);
    if (spec.alt) modifiers.push_back(VK_MENU);
    if (spec.super) modifiers.push_back(VK_LWIN);

    std::vector<INPUT> inputs;
    auto add = [&](WORD vkCode, bool up, bool extended) {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = vkCode;
        in.ki.wScan = static_cast<WORD>(MapVirtualKeyA(vkCode, MAPVK_VK_TO_VSC));
        in.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0)
                      | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
        inputs.push_back(in);
    };
    for (auto m : modifiers) add(m, false, false);
    add(*vk, false, isExtendedKey(spec.key));
    add(*vk, true, isExtendedKey(spec.key));
    for (auto it = modifiers.rbegin(); it != modifiers.rend(); ++it) add(*it, true, false);

    UINT sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    if (sent != inputs.size()) {
        log::warn("AutoDeafen: SendInput only delivered {}/{} events", sent, inputs.size());
    }
}

} // namespace

namespace autodeafen {

bool runningUnderWine() {
    static bool const wine = [] {
        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        return ntdll && GetProcAddress(ntdll, "wine_get_version") != nullptr;
    }();
    return wine;
}

void prepareKeystrokeMode() {
    // no OS permission needed on Windows
}

void tapKey(KeySpec const& spec) {
    if (runningUnderWine()) {
        // Wine-internal SendInput never reaches native Linux apps; the
        // direct delivery mode covers Linux.
        static bool warned = false;
        if (!warned) {
            warned = true;
            log::warn("AutoDeafen: keybind mode does nothing under "
                      "Wine/Proton — switch to the direct delivery mode");
        }
        return;
    }
    tapNative(spec);
}

} // namespace autodeafen

#endif // GEODE_IS_WINDOWS
