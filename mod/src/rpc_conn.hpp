#pragma once

// Discord IPC connection: platform transports + discord-ipc framing.
//
// Discord's local RPC is reachable two ways: a WebSocket server (origin-gated
// against the app's rpc_origins, which unapproved apps cannot set — verified
// empirically, close code 4001) and the IPC channel (named pipe on Windows,
// unix socket elsewhere) which has no origin validation. So we speak IPC:
//   - Windows native: \\.\pipe\discord-ipc-N
//   - macOS: $TMPDIR/discord-ipc-N unix socket
//   - Windows-under-Wine (Linux): the pipe first (works with
//     wine-discord-ipc-bridge), then AF_UNIX through winsock straight at the
//     host's socket (works on Wine builds with AF_UNIX support).

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
