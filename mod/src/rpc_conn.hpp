#pragma once

// Discord IPC connection: platform transports + discord-ipc framing.
//
// Transports, in order:
//   - Windows native: \\.\pipe\discord-ipc-N
//   - macOS: $TMPDIR/discord-ipc-N unix socket
//   - Windows-under-Wine (Linux): the pipe (served by the bundled, auto-
//     launched wine-discord-ipc-bridge), or AF_UNIX through winsock on Wine
//     builds that support it.
// Discord's WebSocket transport validates Origin against the app's
// rpc_origins, which unapproved apps cannot set (close code 4001).

#include <memory>
#include <string>

namespace autodeafen::rpcconn {

enum class Read {
    Ok,       // one JSON message delivered
    Timeout,  // nothing arrived in time; connection still fine
    Closed,   // connection dead (server close, error)
};

class Conn {
public:
    virtual ~Conn() = default;

    // Send one JSON payload as a FRAME message.
    virtual bool sendJson(std::string const& payload) = 0;

    // Read one JSON payload; PINGs are answered internally, CLOSE => Closed.
    virtual Read readJson(std::string& out, int timeoutMs) = 0;

    virtual std::string describe() const = 0;
};

// Try the platform's transports in order and send the discord-ipc HANDSHAKE
// for the given client id. The READY dispatch is left for the caller to read.
// Returns null with a reason in outError if nothing connected.
std::unique_ptr<Conn> open(std::string const& clientId, std::string& outError);

// Listen on 127.0.0.1:port for a single browser OAuth redirect and return the
// "code" query parameter (empty on timeout/denial). Blocks up to timeoutMs;
// worker thread only.
std::string waitForOAuthRedirect(int port, int timeoutMs);

} // namespace autodeafen::rpcconn
