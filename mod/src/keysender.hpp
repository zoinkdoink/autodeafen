#pragma once

#include "core/keyspec.hpp"

namespace autodeafen {

// Tap (press + release) the key on the platform's input system. Fire-and-forget:
// never blocks the game thread, logs and no-ops on failure.
//
// Windows native -> SendInput. macOS -> CGEventPost (needs Accessibility
// permission). Windows-under-Wine (i.e. Linux) -> UDP datagram to the host
// helper, since Wine-internal synthetic input never reaches native apps.
void tapKey(KeySpec const& spec);

// True when the Windows build is running under Wine/Proton. Always false on macOS.
bool runningUnderWine();

// Request whatever OS permission keystroke mode needs, up front (macOS:
// Accessibility prompt). No-op where nothing is needed.
void prepareKeystrokeMode();

} // namespace autodeafen
