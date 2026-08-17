#include <Geode/DefaultInclude.hpp>

#include "discord_rpc.hpp"
#include "core/wire.hpp"
#include "rpc_conn.hpp"

#include <Geode/Geode.hpp>

#include <condition_variable>
#include <mutex>
#include <optional>
#include <random>
#include <thread>

using namespace geode::prelude;

namespace {

namespace wire = autodeafen::wire;
namespace rpcconn = autodeafen::rpcconn;

// The registered AutoDeafen Discord application (a public client: token
// exchange uses PKCE, no secret — validated end to end by spike/rpc_spike.py).
constexpr char const* BUILTIN_CLIENT_ID = "1537095855319031839";
constexpr char const* TOKEN_URL = "https://discord.com/api/oauth2/token";
// Browser-flow fallback; this exact URI must be registered on the Discord app.
constexpr int OAUTH_REDIRECT_PORT = 53535;
constexpr char const* OAUTH_REDIRECT_URI = "http://127.0.0.1:53535";
// Cloudflare rejects default library user agents on discord.com (error 1010)
constexpr char const* USER_AGENT = "AutoDeafen-Geode/0.1";

uint32_t randomU32() {
    static std::mt19937 rng(std::random_device{}());
    return rng();
}

std::string urlencode(std::string_view s) {
    static constexpr char HEX[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += char(c);
        } else {
            out += '%';
            out += HEX[c >> 4];
            out += HEX[c & 15];
        }
    }
    return out;
}

// Saved state is snapshotted on the main thread and handed to the worker,
// which must never touch Mod::get() itself (writes go back via
// queueInMainThread).
struct AuthContext {
    std::string accessToken;
    std::string refreshToken;
};

class RpcClient {
public:
    static RpcClient& get() {
        static RpcClient instance;
        return instance;
    }

    // main thread
    void requestDeafen(bool target) { this->enqueue(target); }
    void connect() { this->enqueue(std::nullopt); }

    void logout() {
        Mod::get()->setSavedValue("discord-access-token", std::string());
        Mod::get()->setSavedValue("discord-refresh-token", std::string());
        Mod::get()->setSavedValue("discord-username", std::string());
        {
            std::lock_guard lock(m_mutex);
            m_auth = {};
            m_username.clear();
            m_pending.reset();
            m_pendingLogout = true;
            if (!m_thread.joinable()) {
                m_thread = std::thread([this] { this->threadMain(); });
            }
        }
        m_cv.notify_one();
    }

    std::string status() {
        std::lock_guard lock(m_mutex);
        return m_status;
    }

    autodeafen::rpc::State state() {
        std::lock_guard lock(m_mutex);
        return m_state;
    }

private:
    // main thread: snapshot settings/saved state, hand work to the worker.
    // nullopt = connect/authorize only.
    void enqueue(std::optional<bool> target) {
        AuthContext auth;
        auth.accessToken = Mod::get()->getSavedValue<std::string>("discord-access-token", "");
        auth.refreshToken = Mod::get()->getSavedValue<std::string>("discord-refresh-token", "");
        {
            std::lock_guard lock(m_mutex);
            m_auth = std::move(auth);
            if (target) m_pending = target;
            else m_pendingConnect = true;
            if (!m_thread.joinable()) {
                m_thread = std::thread([this] { this->threadMain(); });
            }
        }
        m_cv.notify_one();
    }

    ~RpcClient() {
        // The worker holds no resources worth a graceful stop at process exit.
        if (m_thread.joinable()) m_thread.detach();
    }

    void setStatus(autodeafen::rpc::State state, std::string s) {
        std::function<void()> listener;
        {
            std::lock_guard lock(m_mutex);
            if (m_status != s) log::info("AutoDeafen RPC: {}", s);
            m_state = state;
            m_status = std::move(s);
            listener = m_statusListener;
        }
        if (listener) {
            Loader::get()->queueInMainThread([listener] { listener(); });
        }
    }

public:
    void setStatusListener(std::function<void()> callback) {
        std::lock_guard lock(m_mutex);
        m_statusListener = std::move(callback);
    }

private:

    // ---- worker thread from here down ----

    void threadMain() {
        for (;;) {
            std::optional<bool> target;
            bool connectOnly = false;
            bool logout = false;
            {
                std::unique_lock lock(m_mutex);
                m_cv.wait_for(lock, std::chrono::milliseconds(500),
                              [this] { return m_pending.has_value() || m_pendingConnect
                                           || m_pendingLogout; });
                target = m_pending;
                m_pending.reset();
                connectOnly = m_pendingConnect;
                m_pendingConnect = false;
                logout = m_pendingLogout;
                m_pendingLogout = false;
            }
            if (logout) {
                m_liveToken.clear();
                this->disconnect("logged out");
                continue;
            }
            if (m_conn) this->pumpIncoming();
            if (target) {
                if (this->ensureReady()) this->applyDeafen(*target);
                else log::warn("AutoDeafen RPC: dropping deafen({}) — {}",
                               *target, this->status());
            } else if (connectOnly) {
                this->ensureReady();
            }
        }
    }

    AuthContext authSnapshot() {
        std::lock_guard lock(m_mutex);
        return m_auth;
    }

    // Service pings and notice server-side closes between commands.
    void pumpIncoming() {
        std::string payload;
        for (;;) {
            switch (m_conn->readJson(payload, 1)) {
                case rpcconn::Read::Ok:
                    continue;  // unsolicited event between commands; drop
                case rpcconn::Read::Timeout:
                    return;
                case rpcconn::Read::Closed:
                    this->disconnect("connection closed by Discord");
                    return;
            }
        }
    }

    bool ensureReady() {
        if (m_conn && m_authed) return true;
        auto auth = this->authSnapshot();
        if (!m_conn && !this->connectIpc(BUILTIN_CLIENT_ID)) return false;
        if (!m_authed && !this->authenticate(auth)) {
            this->disconnect("authorization failed");
            return false;
        }
        std::string who;
        {
            std::lock_guard lock(m_mutex);
            who = m_username;
        }
        this->setStatus(autodeafen::rpc::State::Connected,
                        who.empty() ? "connected" : "connected as " + who);
        return true;
    }

    bool connectIpc(std::string const& clientId) {
        this->setStatus(autodeafen::rpc::State::Connecting, "connecting to Discord");
        std::string error;
        m_conn = rpcconn::open(clientId, error);
        if (!m_conn) {
            this->setStatus(autodeafen::rpc::State::NotConnected, error);
            return false;
        }
        // first message must be the READY dispatch
        matjson::Value ready;
        if (!this->readMessage(ready, 3000)
            || !ready.get("evt").isOk()
            || ready.get("evt").unwrap().asString().unwrapOr("") != "READY") {
            this->disconnect("no READY dispatch (invalid application ID?)");
            return false;
        }
        return true;
    }

    bool authenticate(AuthContext& auth) {
        if (!auth.accessToken.empty() && this->tryAuthenticate(auth.accessToken)) {
            return true;
        }
        if (!auth.refreshToken.empty()) {
            this->setStatus(autodeafen::rpc::State::Connecting, "refreshing Discord token");
            std::string form = std::string("client_id=") + BUILTIN_CLIENT_ID
                + "&grant_type=refresh_token&refresh_token=" + urlencode(auth.refreshToken);
            if (this->exchangeAndStore(form) && this->tryAuthenticate(m_liveToken)) {
                return true;
            }
        }

        // Full authorize: consent popup inside the Discord client, PKCE flow.
        this->setStatus(autodeafen::rpc::State::AwaitingConsent,
                        "waiting for you to click Authorize in Discord");
        uint8_t verifierBytes[32];
        for (auto& b : verifierBytes) b = uint8_t(randomU32());
        auto verifier = wire::base64UrlEncode(verifierBytes, sizeof(verifierBytes));
        auto digest = wire::sha256(verifier.data(), verifier.size());
        auto challenge = wire::base64UrlEncode(digest.data(), digest.size());

        auto args = matjson::Value::object();
        args.set("client_id", matjson::Value(BUILTIN_CLIENT_ID));
        auto scopes = matjson::Value::array();
        scopes.push("rpc");
        scopes.push("rpc.voice.write");
        args.set("scopes", scopes);
        args.set("code_challenge", matjson::Value(challenge));
        args.set("code_challenge_method", matjson::Value("S256"));
        auto data = this->sendCommand("AUTHORIZE", args, 90000);
        std::string code;
        bool viaBrowser = false;
        if (data) {
            if (auto c = data->get("code"); c.isOk()) {
                code = c.unwrap().asString().unwrapOr("");
            }
        }
        if (code.empty()) {
            // Some Discord builds reject these scopes over the RPC AUTHORIZE
            // command ("invalid_scope") but accept them through the web
            // authorize flow: open the browser and catch the redirect locally.
            this->setStatus(autodeafen::rpc::State::AwaitingConsent,
                            "authorize AutoDeafen in your browser");
            std::string url = std::string("https://discord.com/oauth2/authorize")
                + "?client_id=" + BUILTIN_CLIENT_ID
                + "&response_type=code"
                + "&scope=" + urlencode("rpc rpc.voice.write")
                + "&redirect_uri=" + urlencode(OAUTH_REDIRECT_URI)
                + "&code_challenge=" + challenge
                + "&code_challenge_method=S256";
            Loader::get()->queueInMainThread([url] {
                web::openLinkInBrowser(url);
            });
            code = rpcconn::waitForOAuthRedirect(OAUTH_REDIRECT_PORT, 180000);
            viaBrowser = true;
        }
        if (code.empty()) return false;

        std::string form = std::string("client_id=") + BUILTIN_CLIENT_ID
            + "&grant_type=authorization_code&code=" + urlencode(code)
            + "&code_verifier=" + urlencode(verifier);
        if (viaBrowser) {
            form += "&redirect_uri=" + urlencode(OAUTH_REDIRECT_URI);
        }
        return this->exchangeAndStore(form) && this->tryAuthenticate(m_liveToken);
    }

    bool tryAuthenticate(std::string const& token) {
        auto args = matjson::Value::object();
        args.set("access_token", matjson::Value(token));
        auto data = this->sendCommand("AUTHENTICATE", args, 5000);
        if (!data) return false;
        std::string username;
        if (auto user = data->get("user"); user.isOk()) {
            if (auto name = user.unwrap().get("username"); name.isOk()) {
                username = name.unwrap().asString().unwrapOr("");
            }
        }
        {
            std::lock_guard lock(m_mutex);
            m_username = username;
        }
        if (!username.empty()) {
            // remembered so the settings page can say who authorized even
            // before a connection exists (e.g. Discord closed)
            Loader::get()->queueInMainThread([username] {
                Mod::get()->setSavedValue("discord-username", username);
            });
        }
        m_authed = true;
        m_liveToken = token;
        return true;
    }

    // Synchronous HTTPS on this worker thread (that's what the Sync variants
    // are for). Persists tokens back on the main thread.
    bool exchangeAndStore(std::string const& form) {
        auto response = web::WebRequest()
            .userAgent(USER_AGENT)
            .bodyString(form)
            .header("Content-Type", "application/x-www-form-urlencoded")
            .postSync(TOKEN_URL);
        if (!response.ok()) {
            log::warn("AutoDeafen RPC: token exchange failed (HTTP {}): {}",
                      response.code(), response.string().unwrapOr(""));
            return false;
        }
        auto json = response.json();
        if (!json.isOk()) return false;
        auto body = json.unwrap();
        auto access = body.get("access_token").isOk()
            ? body.get("access_token").unwrap().asString().unwrapOr("") : "";
        if (access.empty()) return false;
        auto refresh = body.get("refresh_token").isOk()
            ? body.get("refresh_token").unwrap().asString().unwrapOr("") : "";
        m_liveToken = access;
        Loader::get()->queueInMainThread([access, refresh] {
            Mod::get()->setSavedValue("discord-access-token", access);
            if (!refresh.empty()) Mod::get()->setSavedValue("discord-refresh-token", refresh);
        });
        return true;
    }

    void applyDeafen(bool target) {
        auto current = this->getDeafState();
        if (!current) return;
        if (target) {
            if (*current) {
                // Already deafened (maybe manually) — leave their state alone.
                m_weDeafened = false;
                return;
            }
            if (this->setDeafState(true)) {
                m_weDeafened = true;
                log::info("AutoDeafen RPC: deafened");
            }
        } else {
            if (!m_weDeafened) return;
            m_weDeafened = false;
            if (*current && this->setDeafState(false)) {
                log::info("AutoDeafen RPC: un-deafened");
            }
        }
    }

    std::optional<bool> getDeafState() {
        auto data = this->sendCommand("GET_VOICE_SETTINGS", matjson::Value::object(), 5000);
        if (!data) return std::nullopt;
        if (auto deaf = data->get("deaf"); deaf.isOk()) {
            return deaf.unwrap().asBool().unwrapOr(false);
        }
        return std::nullopt;
    }

    bool setDeafState(bool deaf) {
        auto args = matjson::Value::object();
        args.set("deaf", matjson::Value(deaf));
        return this->sendCommand("SET_VOICE_SETTINGS", args, 5000).has_value();
    }

    // Send an RPC command and wait for the nonce-matched response. Returns its
    // "data" payload, or nullopt on error/timeout.
    std::optional<matjson::Value> sendCommand(
        std::string const& cmd, matjson::Value args, int timeoutMs
    ) {
        if (!m_conn) return std::nullopt;
        auto nonce = "ad-" + std::to_string(++m_nonce);
        auto msg = matjson::Value::object();
        msg.set("cmd", matjson::Value(cmd));
        msg.set("args", std::move(args));
        msg.set("nonce", matjson::Value(nonce));
        if (!m_conn->sendJson(msg.dump(matjson::NO_INDENTATION))) {
            this->disconnect("send failed");
            return std::nullopt;
        }
        auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(timeoutMs);
        matjson::Value reply;
        while (std::chrono::steady_clock::now() < deadline) {
            auto remaining = int(std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count());
            if (!this->readMessage(reply, std::max(1, remaining))) return std::nullopt;
            auto gotNonce = reply.get("nonce").isOk()
                ? reply.get("nonce").unwrap().asString().unwrapOr("") : "";
            if (gotNonce != nonce) continue;  // unsolicited event
            auto evt = reply.get("evt").isOk()
                ? reply.get("evt").unwrap().asString().unwrapOr("") : "";
            if (evt == "ERROR") {
                log::warn("AutoDeafen RPC: {} failed: {}", cmd,
                          reply.get("data").isOk()
                              ? reply.get("data").unwrap().dump(matjson::NO_INDENTATION)
                              : "?");
                return std::nullopt;
            }
            if (reply.get("data").isOk()) return reply.get("data").unwrap();
            return matjson::Value::object();
        }
        return std::nullopt;
    }

    // Read one complete JSON message; false on timeout or connection loss.
    bool readMessage(matjson::Value& out, int timeoutMs) {
        if (!m_conn) return false;
        std::string payload;
        switch (m_conn->readJson(payload, timeoutMs)) {
            case rpcconn::Read::Timeout:
                return false;
            case rpcconn::Read::Closed:
                this->disconnect("connection closed by Discord");
                return false;
            case rpcconn::Read::Ok:
                break;
        }
        auto parsed = matjson::parse(payload);
        if (!parsed.isOk()) return false;
        out = parsed.unwrap();
        return true;
    }

    void disconnect(char const* why) {
        if (m_conn) {
            m_conn.reset();
            log::debug("AutoDeafen RPC: disconnected ({})", why);
        }
        m_authed = false;
        m_weDeafened = false;  // Discord resets controlled settings on disconnect
        this->setStatus(autodeafen::rpc::State::NotConnected, why);
    }

    // shared state (mutex)
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::optional<bool> m_pending;
    bool m_pendingConnect = false;
    bool m_pendingLogout = false;
    AuthContext m_auth;
    autodeafen::rpc::State m_state = autodeafen::rpc::State::NotConnected;
    std::string m_status = "not connected";
    std::string m_username;
    std::function<void()> m_statusListener;
    std::thread m_thread;

    // worker-only state
    std::unique_ptr<rpcconn::Conn> m_conn;
    bool m_authed = false;
    bool m_weDeafened = false;
    std::string m_liveToken;
    int m_nonce = 0;
};

} // namespace

namespace autodeafen::rpc {

void requestDeafen(bool deafened) {
    RpcClient::get().requestDeafen(deafened);
}

void connect() {
    RpcClient::get().connect();
}

void logout() {
    RpcClient::get().logout();
}

void setStatusListener(std::function<void()> callback) {
    RpcClient::get().setStatusListener(std::move(callback));
}

State state() {
    return RpcClient::get().state();
}

std::string status() {
    return RpcClient::get().status();
}

} // namespace autodeafen::rpc
