#include <Geode/DefaultInclude.hpp>

#include "rpc_conn.hpp"
#include "core/wire.hpp"
#include "keysender.hpp"  // runningUnderWine()

#ifdef GEODE_IS_WINDOWS
    // Geode's PCH force-includes <windows.h> (with the original winsock.h)
    // before this file; everything used here is in that winsock 1.1 surface.
    #include <windows.h>
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <sys/un.h>
    #include <unistd.h>
#endif

#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace geode::prelude;

namespace autodeafen::rpcconn {

namespace {

namespace wire = autodeafen::wire;

using Clock = std::chrono::steady_clock;

int msUntil(Clock::time_point deadline) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - Clock::now()).count();
    return int(std::max<long long>(0, ms));
}

// discord-ipc framing over a byte stream provided by a subclass.
class IpcConnBase : public Conn {
public:
    bool sendJson(std::string const& payload) override {
        return this->sendMessage(wire::IpcOp::Frame, payload);
    }

    Read readJson(std::string& out, int timeoutMs) override {
        auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        for (;;) {
            uint8_t header[8];
            auto r = this->readExact(header, 8, deadline);
            if (r != Read::Ok) return r;
            auto parsed = wire::parseIpcHeader(header);
            if (parsed.length > 1024 * 1024) return Read::Closed;

            std::string payload(parsed.length, '\0');
            // once a header arrived, the payload follows immediately; give it
            // a little extra grace past the caller's deadline
            auto payloadDeadline = std::max(
                deadline, Clock::now() + std::chrono::milliseconds(2000));
            if (parsed.length > 0
                && this->readExact(reinterpret_cast<uint8_t*>(payload.data()),
                                   payload.size(), payloadDeadline) != Read::Ok) {
                return Read::Closed;
            }

            switch (parsed.op) {
                case wire::IpcOp::Ping:
                    this->sendMessage(wire::IpcOp::Pong, payload);
                    continue;
                case wire::IpcOp::Close:
                    log::debug("AutoDeafen RPC: server close: {}", payload);
                    return Read::Closed;
                case wire::IpcOp::Frame:
                case wire::IpcOp::Handshake:
                    out = std::move(payload);
                    return Read::Ok;
                default:
                    continue;  // ignore pongs / unknown ops
            }
        }
    }

    bool sendMessage(wire::IpcOp op, std::string const& payload) {
        auto header = wire::ipcHeader(op, uint32_t(payload.size()));
        std::vector<uint8_t> buf(header.begin(), header.end());
        buf.insert(buf.end(), payload.begin(), payload.end());
        return this->sendBytes(buf.data(), buf.size());
    }

protected:
    virtual bool sendBytes(uint8_t const* data, size_t n) = 0;
    // Read some bytes; Read::Timeout if none arrived before the deadline.
    virtual Read recvSome(uint8_t* buf, size_t max, size_t& got, int timeoutMs) = 0;

    Read readExact(uint8_t* buf, size_t n, Clock::time_point deadline) {
        size_t have = 0;
        bool started = false;
        while (have < n) {
            int wait = msUntil(deadline);
            if (wait == 0 && Clock::now() >= deadline) {
                // partial reads mid-message mean the stream broke, not idle
                return started ? Read::Closed : Read::Timeout;
            }
            size_t got = 0;
            auto r = this->recvSome(buf + have, n - have, got, std::min(wait, 250));
            if (r == Read::Closed) return Read::Closed;
            if (got > 0) {
                have += got;
                started = true;
            }
        }
        return Read::Ok;
    }
};

#ifndef GEODE_IS_WINDOWS

// ---- macOS: unix socket ----

class UnixSocketConn : public IpcConnBase {
public:
    static std::unique_ptr<UnixSocketConn> tryConnect(std::string const& path) {
        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return nullptr;
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (path.size() >= sizeof(addr.sun_path)) {
            ::close(fd);
            return nullptr;
        }
        std::strcpy(addr.sun_path, path.c_str());
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            return nullptr;
        }
        auto conn = std::make_unique<UnixSocketConn>();
        conn->m_fd = fd;
        conn->m_path = path;
        return conn;
    }

    ~UnixSocketConn() override {
        if (m_fd >= 0) ::close(m_fd);
    }

    std::string describe() const override { return "unix socket " + m_path; }

protected:
    bool sendBytes(uint8_t const* data, size_t n) override {
        size_t sent = 0;
        while (sent < n) {
            auto w = ::send(m_fd, data + sent, n - sent, 0);
            if (w <= 0) return false;
            sent += size_t(w);
        }
        return true;
    }

    Read recvSome(uint8_t* buf, size_t max, size_t& got, int timeoutMs) override {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(m_fd, &rfds);
        timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
        int sel = ::select(m_fd + 1, &rfds, nullptr, nullptr, &tv);
        if (sel == 0) return Read::Timeout;
        if (sel < 0) return Read::Closed;
        auto r = ::recv(m_fd, buf, max, 0);
        if (r <= 0) return Read::Closed;
        got = size_t(r);
        return Read::Ok;
    }

private:
    int m_fd = -1;
    std::string m_path;
};

#else

// ---- Windows: named pipe (native Discord; Wine with an ipc bridge) ----

class PipeConn : public IpcConnBase {
public:
    static std::unique_ptr<PipeConn> tryConnect(std::string const& path) {
        HANDLE h = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                               0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) return nullptr;
        auto conn = std::make_unique<PipeConn>();
        conn->m_pipe = h;
        conn->m_path = path;
        return conn;
    }

    ~PipeConn() override {
        if (m_pipe != INVALID_HANDLE_VALUE) CloseHandle(m_pipe);
    }

    std::string describe() const override { return "pipe " + m_path; }

protected:
    bool sendBytes(uint8_t const* data, size_t n) override {
        size_t sent = 0;
        while (sent < n) {
            DWORD written = 0;
            if (!WriteFile(m_pipe, data + sent, DWORD(n - sent), &written, nullptr)
                || written == 0) {
                return false;
            }
            sent += written;
        }
        return true;
    }

    Read recvSome(uint8_t* buf, size_t max, size_t& got, int timeoutMs) override {
        auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        for (;;) {
            DWORD avail = 0;
            if (!PeekNamedPipe(m_pipe, nullptr, 0, nullptr, &avail, nullptr)) {
                return Read::Closed;
            }
            if (avail > 0) break;
            if (Clock::now() >= deadline) return Read::Timeout;
            Sleep(15);
        }
        DWORD read = 0;
        if (!ReadFile(m_pipe, buf, DWORD(max), &read, nullptr) || read == 0) {
            return Read::Closed;
        }
        got = read;
        return Read::Ok;
    }

private:
    HANDLE m_pipe = INVALID_HANDLE_VALUE;
    std::string m_path;
};

// ---- Windows-under-Wine: AF_UNIX through winsock at the host's socket ----
// Windows has supported AF_UNIX since Win10; Wine grew support in the
// staging ws2_32-af_unix patchset (10.2+). On builds without it, socket()
// fails fast with WSAEAFNOSUPPORT and we move on.

constexpr int WIN_AF_UNIX = 1;
struct sockaddr_un_w {
    unsigned short sun_family;
    char sun_path[108];
};

class WineUnixSocketConn : public IpcConnBase {
public:
    static std::unique_ptr<WineUnixSocketConn> tryConnect(std::string const& path) {
        static bool const wsaReady = [] {
            WSADATA wsa;
            return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
        }();
        if (!wsaReady) return nullptr;
        SOCKET s = ::socket(WIN_AF_UNIX, SOCK_STREAM, 0);
        if (s == INVALID_SOCKET) return nullptr;
        sockaddr_un_w addr{};
        addr.sun_family = WIN_AF_UNIX;
        if (path.size() >= sizeof(addr.sun_path)) {
            closesocket(s);
            return nullptr;
        }
        std::strcpy(addr.sun_path, path.c_str());
        if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            closesocket(s);
            return nullptr;
        }
        auto conn = std::make_unique<WineUnixSocketConn>();
        conn->m_sock = s;
        conn->m_path = path;
        return conn;
    }

    ~WineUnixSocketConn() override {
        if (m_sock != INVALID_SOCKET) closesocket(m_sock);
    }

    std::string describe() const override { return "wine unix socket " + m_path; }

protected:
    bool sendBytes(uint8_t const* data, size_t n) override {
        size_t sent = 0;
        while (sent < n) {
            int w = ::send(m_sock, reinterpret_cast<char const*>(data + sent),
                           int(n - sent), 0);
            if (w <= 0) return false;
            sent += size_t(w);
        }
        return true;
    }

    Read recvSome(uint8_t* buf, size_t max, size_t& got, int timeoutMs) override {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(m_sock, &rfds);
        timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
        int sel = ::select(0, &rfds, nullptr, nullptr, &tv);
        if (sel == 0) return Read::Timeout;
        if (sel < 0) return Read::Closed;
        int r = ::recv(m_sock, reinterpret_cast<char*>(buf), int(max), 0);
        if (r <= 0) return Read::Closed;
        got = size_t(r);
        return Read::Ok;
    }

private:
    SOCKET m_sock = INVALID_SOCKET;
    std::string m_path;
};

#endif // GEODE_IS_WINDOWS

std::string envVar(char const* name) {
#ifdef GEODE_IS_WINDOWS
    char buf[512];
    DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) return "";
    return std::string(buf, n);
#else
    char const* v = std::getenv(name);
    return v ? v : "";
#endif
}

// Candidate discord-ipc socket paths on the host, in discovery order.
std::vector<std::string> unixSocketCandidates() {
    std::vector<std::string> dirs;
    for (auto var : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"}) {
        auto v = envVar(var);
        if (!v.empty()) {
            if (v.back() != '/') v += '/';
            dirs.push_back(v);
        }
    }
    dirs.push_back("/tmp/");

    std::vector<std::string> out;
    for (auto const& dir : dirs) {
        for (auto sub : {"", "app/com.discordapp.Discord/", "snap.discord/"}) {
            for (int n = 0; n < 10; ++n) {
                out.push_back(dir + sub + "discord-ipc-" + std::to_string(n));
            }
        }
    }
    return out;
}

#ifdef GEODE_IS_WINDOWS

std::unique_ptr<Conn> tryPipes() {
    for (int n = 0; n < 10; ++n) {
        auto path = "\\\\.\\pipe\\discord-ipc-" + std::to_string(n);
        if (auto conn = PipeConn::tryConnect(path)) return conn;
    }
    return nullptr;
}

// Last rung under Wine: launch the bundled wine-discord-ipc-bridge (MIT, by
// 0e4ef622). It reaches the host's Discord socket via raw Linux syscalls, so
// it works on any Wine/Proton regardless of AF_UNIX support, and serves the
// pipe transport back to us. Relaunched on later connect attempts if the
// previous instance died (e.g. Discord wasn't running yet); a job object
// ties its lifetime to the game process.
bool launchBundledBridge() {
    static HANDLE s_bridge = nullptr;
    if (s_bridge) {
        DWORD code = 0;
        if (GetExitCodeProcess(s_bridge, &code) && code == STILL_ACTIVE) {
            // already running and the pipe still wasn't reachable; no point
            // waiting on it again
            return false;
        }
        CloseHandle(s_bridge);
        s_bridge = nullptr;
    }

    auto exe = (geode::Mod::get()->getResourcesDir()
                / "winediscordipcbridge.exe").string();
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(exe.c_str(), nullptr, nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        log::warn("AutoDeafen: could not launch bundled ipc bridge ({}): error {}",
                  exe, GetLastError());
        return false;
    }
    static HANDLE const job = [] {
        HANDLE j = CreateJobObjectA(nullptr, nullptr);
        if (j) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(j, JobObjectExtendedLimitInformation,
                                    &info, sizeof(info));
        }
        return j;
    }();
    if (job) AssignProcessToJobObject(job, pi.hProcess);
    CloseHandle(pi.hThread);
    s_bridge = pi.hProcess;
    log::info("AutoDeafen: launched bundled wine-discord-ipc-bridge");
    return true;
}

#endif // GEODE_IS_WINDOWS

#ifdef GEODE_IS_WINDOWS
// One info-level line per fact so a single tester log answers everything.
void logWineEnvironmentOnce() {
    static bool logged = false;
    if (logged) return;
    logged = true;
    using VersionFn = char const*(CDECL*)();
    auto fn = reinterpret_cast<VersionFn>(
        GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version"));
    log::info("AutoDeafen RPC: running under Wine {} — XDG_RUNTIME_DIR='{}' TMPDIR='{}'",
              fn ? fn() : "(unknown version)",
              envVar("XDG_RUNTIME_DIR"), envVar("TMPDIR"));
}
#endif

std::unique_ptr<Conn> openTransport() {
#ifdef GEODE_IS_WINDOWS
    if (auto conn = tryPipes()) return conn;
    if (autodeafen::runningUnderWine()) {
        logWineEnvironmentOnce();
        log::info("AutoDeafen RPC: rung 1: no discord-ipc pipe found");

        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        SOCKET probe = ::socket(1 /* AF_UNIX */, SOCK_STREAM, 0);
        if (probe == INVALID_SOCKET) {
            log::info("AutoDeafen RPC: rung 2: this Wine has no AF_UNIX support "
                      "(error {})", WSAGetLastError());
        } else {
            closesocket(probe);
            int tried = 0;
            for (auto const& path : unixSocketCandidates()) {
                ++tried;
                if (auto conn = WineUnixSocketConn::tryConnect(path)) return conn;
            }
            log::info("AutoDeafen RPC: rung 2: AF_UNIX supported but no Discord "
                      "socket connected ({} candidate paths)", tried);
        }

        if (launchBundledBridge()) {
            // the bridge needs a moment to reach Discord and create the pipe
            for (int i = 0; i < 16; ++i) {
                Sleep(250);
                if (auto conn = tryPipes()) return conn;
            }
            log::info("AutoDeafen RPC: rung 3: bridge launched but no pipe "
                      "appeared within 4s (is Discord running on the host?)");
        }
    }
    return nullptr;
#else
    for (auto const& path : unixSocketCandidates()) {
        if (auto conn = UnixSocketConn::tryConnect(path)) return conn;
    }
    return nullptr;
#endif
}

} // namespace

std::string waitForOAuthRedirect(int port, int timeoutMs) {
#ifdef GEODE_IS_WINDOWS
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    using ListenSock = SOCKET;
    constexpr ListenSock BAD = INVALID_SOCKET;
#else
    using ListenSock = int;
    constexpr ListenSock BAD = -1;
#endif
    ListenSock server = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server == BAD) return "";
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<char const*>(&reuse), sizeof(reuse));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(uint16_t(port));
    addr.sin_addr.s_addr = htonl(0x7f000001);  // 127.0.0.1
    auto closeListen = [&](ListenSock s) {
#ifdef GEODE_IS_WINDOWS
        closesocket(s);
#else
        ::close(s);
#endif
    };
    if (::bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0
        || ::listen(server, 1) != 0) {
        log::warn("AutoDeafen RPC: could not listen on 127.0.0.1:{} for the "
                  "browser redirect", port);
        closeListen(server);
        return "";
    }

    auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    std::string code;
    while (Clock::now() < deadline) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(server, &rfds);
        timeval tv{1, 0};
#ifdef GEODE_IS_WINDOWS
        int sel = ::select(0, &rfds, nullptr, nullptr, &tv);
#else
        int sel = ::select(server + 1, &rfds, nullptr, nullptr, &tv);
#endif
        if (sel <= 0) continue;
        ListenSock client = ::accept(server, nullptr, nullptr);
        if (client == BAD) continue;

        char buf[4096];
        int n = ::recv(client, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = 0;
            std::string request(buf);
            // GET /?code=XXXX&... HTTP/1.1
            auto pos = request.find("code=");
            if (pos != std::string::npos) {
                pos += 5;
                auto end = request.find_first_of("& \r\n", pos);
                code = request.substr(pos, end - pos);
            }
            char const* body = code.empty()
                ? "<h2>Authorization failed or was denied.</h2>"
                  "<p>You can close this tab.</p>"
                : "<h2>AutoDeafen is authorized!</h2>"
                  "<p>You can close this tab and return to the game.</p>";
            std::string response =
                "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close"
                "\r\n\r\n" + std::string(body);
            ::send(client, response.data(), int(response.size()), 0);
        }
        closeListen(client);
        if (!code.empty()) break;
        // no code (user denied, or a stray request): keep listening until the
        // deadline in case the real redirect is still coming
    }
    closeListen(server);
    return code;
}

std::unique_ptr<Conn> open(std::string const& clientId, std::string& outError) {
    auto conn = openTransport();
    if (!conn) {
#ifdef GEODE_IS_WINDOWS
        if (autodeafen::runningUnderWine()) {
            outError = "can't reach Discord: pipe, AF_UNIX, and bundled "
                       "bridge all failed — is Discord running on the host?";
            return nullptr;
        }
#endif
        outError = "no Discord IPC endpoint found (is the desktop client running?)";
        return nullptr;
    }
    auto handshake = std::string(R"({"v":1,"client_id":")") + clientId + R"("})";
    auto* ipc = static_cast<IpcConnBase*>(conn.get());
    if (!ipc->sendMessage(wire::IpcOp::Handshake, handshake)) {
        outError = "handshake send failed on " + conn->describe();
        return nullptr;
    }
    log::info("AutoDeafen RPC: connected via {}", conn->describe());
    return conn;
}

} // namespace autodeafen::rpcconn
