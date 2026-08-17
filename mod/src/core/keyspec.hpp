#pragma once

// A key tap: modifiers + one key name. Built from the recorded Geode keybind
// setting (see deafen.cpp) and consumed by the platform key senders. Key
// names are lowercase: a-z, 0-9, f1-f24, and named keys like "space",
// "backslash", "pageup".

#include <string>

namespace autodeafen {

struct KeySpec {
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    bool super = false;
    std::string key;

    bool operator==(KeySpec const&) const = default;
};

} // namespace autodeafen
