#pragma once

#include <functional>
#include <string>

namespace autodeafen::rpc {

// Ask Discord to reach the given deafen state, via its local RPC server.
// Queued to a worker thread; never blocks the game thread. Semantics are
// absolute and polite: deafen only if the user isn't already deafened,
// un-deafen only if we were the ones who deafened. Drops the request with a
// log line if Discord isn't running or authorization fails.
//
// Must be called from the main thread (reads mod settings/saved values).
void requestDeafen(bool deafened);

// Connect and authorize eagerly without touching deafen state — used by the
// settings UI so the Discord consent popup appears while the user is watching
// for it, and at startup to silently resume a previous authorization.
// Main thread only.
void connect();

// Forget the stored authorization (tokens + username) and drop the
// connection. The next connect() shows the consent popup again. Main thread.
void logout();

// Coarse connection phase, for UI that needs to branch on state.
enum class State {
    NotConnected,    // no live connection (authorization may still be stored)
    Connecting,      // connecting / refreshing tokens
    AwaitingConsent, // Authorize popup is up in Discord
    Connected,       // connected and authenticated
};
State state();

// Human-readable detail ("connected as <user>", "waiting for you to click
// Authorize in Discord", ...) for logs and the settings status line.
std::string status();

// Register a callback invoked on the main thread whenever state()/status()
// change, so UI can refresh immediately instead of polling. One listener;
// pass nullptr to clear.
void setStatusListener(std::function<void()> callback);

} // namespace autodeafen::rpc
