#pragma once

namespace autodeafen::deafen {

// Bring Discord to the given deafen state via the configured delivery mode:
// "discord" (local RPC, default) or "keystroke" (tap the configured key,
// toggle semantics with an internal belief of the current state). Idempotent
// and safe to call when nothing needs to change. Main thread only.
void request(bool deafened);

} // namespace autodeafen::deafen
